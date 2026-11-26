#include "rhydb/query_engine/exec_node/table_scan.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <roaring/roaring.h>
#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/batched_bitmap_reader.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/data_type_visitor.h"
#include "rhydb/storage/sequence/sequence_chunk.h"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

namespace rhydb::query_engine::exec_node {

namespace {

/// The texts of the non-null rows `non_null_rows` of `chunk`, in ascending row order
std::vector<std::string> reconstructNonNullSequences(
   const storage::sequence::SequenceChunk& chunk,
   roaring_util::RoaringContainerView non_null_rows
) {
   std::vector<std::string> sequences(
      non_null_rows.getCardinality(), std::string(chunk.sequenceLength(), '\0')
   );
   std::vector<char*> texts;
   texts.reserve(sequences.size());
   for (auto& sequence : sequences) {
      texts.push_back(sequence.data());
   }
   storage::sequence::reconstructSequences(chunk, non_null_rows, texts);
   return sequences;
}

/// Appends the selected `rows` of `chunk`: null for a null row, else the compressed text
arrow::Status appendChunkSequences(
   const storage::sequence::SequenceChunk& chunk,
   roaring_util::RoaringContainerView rows,
   rhydb::ZstdCompressor& compressor,
   arrow::LargeBinaryBuilder& output_array
) {
   const roaring_util::RoaringContainer non_null_rows = rows - chunk.nullRows();
   const auto reconstructed_sequences =
      reconstructNonNullSequences(chunk, roaring_util::RoaringContainerView{non_null_rows});
   auto next_sequence = reconstructed_sequences.begin();
   for (const uint16_t row : rows) {
      if (chunk.isNull(row)) {
         ARROW_RETURN_NOT_OK(output_array.AppendNull());
         continue;
      }
      ARROW_RETURN_NOT_OK(
         output_array.Append(compressor.compress(next_sequence->data(), next_sequence->size()))
      );
      ++next_sequence;
   }
   return arrow::Status::OK();
}

template <typename SymbolType>
arrow::Status appendSequences(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   const Bitmap& row_ids,
   arrow::LargeBinaryBuilder& output_array
) {
   ARROW_RETURN_NOT_OK(output_array.Reserve(static_cast<int64_t>(row_ids.cardinality())));
   auto reference_sequence =
      SymbolType::sequenceToString(sequence_column.metadata->reference_sequence);
   auto dictionary = std::make_shared<rhydb::ZstdCDictionary>(reference_sequence, 3);
   rhydb::ZstdCompressor compressor{dictionary};

   for (const auto& [chunk_id, rows] : row_ids) {
      ARROW_RETURN_NOT_OK(
         appendChunkSequences(sequence_column.chunk(chunk_id), rows, compressor, output_array)
      );
   }
   return arrow::Status::OK();
}

class ColumnEntryAppender {
  public:
   template <storage::column::Column Column>
   arrow::Status operator()(
      ExecBatchBuilder& table_scan_node,
      const std::string& column_name,
      const storage::Table& table,
      const Bitmap& row_ids
   );
};

template <>
arrow::Status ColumnEntryAppender::operator()<storage::column::SequenceColumn<Nucleotide>>(
   ExecBatchBuilder& table_scan_node,
   const std::string& column_name,
   const storage::Table& table,
   const Bitmap& row_ids
) {
   auto* array =
      table_scan_node.getColumnTypeArrayBuilders<storage::column::SequenceColumn<Nucleotide>>().at(
         column_name
      );
   return appendSequences<Nucleotide>(
      table.getColumn<storage::column::SequenceColumn<Nucleotide>>(column_name), row_ids, *array
   );
}

template <>
arrow::Status ColumnEntryAppender::operator()<storage::column::SequenceColumn<AminoAcid>>(
   ExecBatchBuilder& table_scan_node,
   const std::string& column_name,
   const storage::Table& table,
   const Bitmap& row_ids
) {
   auto* array =
      table_scan_node.getColumnTypeArrayBuilders<storage::column::SequenceColumn<AminoAcid>>().at(
         column_name
      );
   return appendSequences<AminoAcid>(
      table.getColumn<storage::column::SequenceColumn<AminoAcid>>(column_name), row_ids, *array
   );
}

template <>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status ColumnEntryAppender::operator()<storage::column::ZstdCompressedStringColumn>(
   ExecBatchBuilder& table_scan_node,
   const std::string& column_name,
   const storage::Table& table,
   const Bitmap& row_ids
) {
   auto* array =
      table_scan_node.getColumnTypeArrayBuilders<storage::column::ZstdCompressedStringColumn>().at(
         column_name
      );
   const auto& column = table.getColumn<storage::column::ZstdCompressedStringColumn>(column_name);
   for (const auto& [chunk_key, view] : row_ids) {
      const uint32_t base = static_cast<uint32_t>(chunk_key) << 16U;
      for (const uint16_t low_bits : view) {
         auto value = column.getCompressed(storage::column::RowId::fromGlobal(base | low_bits));
         if (value.has_value()) {
            ARROW_RETURN_NOT_OK(array->Append(value.value()));
         } else {
            ARROW_RETURN_NOT_OK(array->AppendNull());
         }
      }
   }
   return arrow::Status::OK();
}

template <storage::column::Column Column>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status ColumnEntryAppender::operator()(
   ExecBatchBuilder& table_scan_node,
   const std::string& column_name,
   const storage::Table& table,
   const Bitmap& row_ids
) {
   auto& column = table.getColumn<Column>(column_name);
   auto array = table_scan_node.getColumnTypeArrayBuilders<Column>().at(column_name);
   for (const auto& [chunk_key, view] : row_ids) {
      const uint32_t base = static_cast<uint32_t>(chunk_key) << 16U;
      for (const uint16_t low_bits : view) {
         const auto row_id = storage::column::RowId::fromGlobal(base | low_bits);
         if (column.isNull(row_id)) {
            ARROW_RETURN_NOT_OK(array->AppendNull());
         } else {
            if constexpr (std::is_same_v<Column, storage::column::StringColumn>) {
               auto value = column.getValueString(row_id);
               ARROW_RETURN_NOT_OK(array->Append(value));
            } else if constexpr (Column::KIND == schema::DataType::Kind::DICTIONARY_ENCODED) {
               auto value = column.lookupValue(column.getValue(row_id));
               ARROW_RETURN_NOT_OK(array->Append(value));
            } else {
               auto value = column.getValue(row_id);
               ARROW_RETURN_NOT_OK(array->Append(value));
            }
         }
      }
   }
   return arrow::Status::OK();
}

}  // namespace

ExecBatchBuilder::ExecBatchBuilder(std::vector<rhydb::schema::ColumnIdentifier> output_fields_)
    : output_fields(std::move(output_fields_)) {
   for (const auto& [name, type] : output_fields) {
      storage::column::visit(type, [&]<storage::column::Column Column> {
         array_builders[type].emplace(name, std::make_shared<ArrowBuilder<Column>>());
      });
   }
}

arrow::Status ExecBatchBuilder::appendEntries(const storage::Table& table, const Bitmap& row_ids) {
   EVOBENCH_SCOPE("ExecBatchBuilder", "appendEntries");
   for (const auto& field : output_fields) {
      ARROW_RETURN_NOT_OK(
         storage::column::visit(
            field.type, ColumnEntryAppender{}, *this, field.name, table, row_ids
         )
      );
   }
   return arrow::Status::OK();
}

arrow::Result<arrow::ExecBatch> ExecBatchBuilder::finishBatch(int64_t length) {
   EVOBENCH_SCOPE("ExecBatchBuilder", "finishBatch");
   std::vector<arrow::Datum> data;
   for (auto& field : output_fields) {
      auto status = storage::column::visit(field.type, [&]<storage::column::Column Column> {
         ARROW_ASSIGN_OR_RAISE(
            auto array, getColumnTypeArrayBuilders<Column>().at(field.name)->Finish()
         );
         data.push_back(array);
         return arrow::Status::OK();
      });
      ARROW_RETURN_NOT_OK(status);
   }
   // Pass the row count explicitly: a zero-column batch (e.g. a bare count(*) whose scan needs no
   // columns) has no arrays to infer the length from, and Arrow rejects ExecBatch::Make without it.
   return arrow::compute::ExecBatch::Make(std::move(data), length);
}

arrow::Result<std::optional<arrow::ExecBatch>> TableScanGenerator::operator()() {
   EVOBENCH_SCOPE("TableScanGenerator", "produceNextBatch");
   if (!filter_computed) {
      current_bitmap_reader =
         BatchedBitmapReader{filter->evaluate().toRoaring(), batch_size_cutoff};
      filter_computed = true;
   }
   while (current_bitmap_reader.has_value()) {
      auto row_ids = current_bitmap_reader.value().nextBatch();
      if (row_ids.has_value()) {
         const Bitmap batch_row_ids{std::move(row_ids.value())};
         const auto length = static_cast<int64_t>(batch_row_ids.cardinality());
         ARROW_RETURN_NOT_OK(exec_batch_builder.appendEntries(*table, batch_row_ids));
         ARROW_ASSIGN_OR_RAISE(auto batch, exec_batch_builder.finishBatch(length));
         SPDLOG_DEBUG("Finished arrow::ExecBatch with length: {}", batch.length);
         return batch;
      }
      current_bitmap_reader = std::nullopt;
   }
   return std::nullopt;
}

arrow::Result<arrow::acero::ExecNode*> makeTableScan(
   arrow::acero::ExecPlan* plan,
   const std::vector<rhydb::schema::ColumnIdentifier>& columns,
   std::shared_ptr<const operators::CompiledFilter> filter,
   std::shared_ptr<const storage::Table> table,
   size_t batch_size_cutoff
) {
   const arrow::acero::SourceNodeOptions source_node_options{
      exec_node::columnsToArrowSchema(columns),
      makeAsyncBatchGenerator(
         *plan, TableScanGenerator{columns, std::move(filter), std::move(table), batch_size_cutoff}
      ),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", plan, {}, source_node_options);
}

}  // namespace rhydb::query_engine::exec_node
