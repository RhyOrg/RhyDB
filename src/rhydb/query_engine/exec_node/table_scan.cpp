#include "rhydb/query_engine/exec_node/table_scan.h"
#include <utility>

#include <roaring/containers/array.h>
#include <roaring/containers/bitset.h>
#include <roaring/containers/containers.h>
#include <roaring/containers/run.h>
#include <roaring/roaring.h>
#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/batched_bitmap_reader.h"
#include "rhydb/storage/column/column_type_visitor.h"

namespace rhydb::query_engine::exec_node {

namespace {

template <typename SymbolType>
std::vector<std::string> reconstructNonNullSequences(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   const Bitmap& non_null_row_ids
) {
   const std::string reference = sequence_column.local_reference_sequence_string;

   std::vector<std::string> reconstructed_sequences;
   reconstructed_sequences.resize(non_null_row_ids.cardinality(), reference);

   sequence_column.vertical_sequence_index.overwriteSymbolsInSequences(
      reconstructed_sequences, non_null_row_ids
   );

   sequence_column.horizontal_coverage_index.template overwriteCoverageInSequence<SymbolType>(
      reconstructed_sequences, non_null_row_ids
   );
   return reconstructed_sequences;
}

template <typename SymbolType>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status appendSequences(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   const Bitmap& row_ids,
   arrow::BinaryBuilder& output_array
) {
   const Bitmap non_null_row_ids = row_ids - Bitmap{&sequence_column.null_bitmap};
   auto reconstructed_non_null_sequences =
      reconstructNonNullSequences(sequence_column, non_null_row_ids);

   ARROW_RETURN_NOT_OK(output_array.Reserve(static_cast<int64_t>(row_ids.cardinality())));
   auto reference_sequence =
      SymbolType::sequenceToString(sequence_column.metadata->reference_sequence);
   auto dictionary = std::make_shared<rhydb::ZstdCDictionary>(reference_sequence, 3);
   rhydb::ZstdCompressor compressor{dictionary};

   auto reconstructed_sequence_iterator = reconstructed_non_null_sequences.begin();
   for (const auto& [chunk_key, view] : row_ids) {
      const uint32_t base = static_cast<uint32_t>(chunk_key) << 16U;
      for (const uint16_t low_bits : view) {
         if (sequence_column.isNull(storage::column::RowId::fromGlobal(base | low_bits))) {
            ARROW_RETURN_NOT_OK(output_array.AppendNull());
         } else {
            auto& reconstructed_sequence = *reconstructed_sequence_iterator;
            ARROW_RETURN_NOT_OK(output_array.Append(
               compressor.compress(reconstructed_sequence.data(), reconstructed_sequence.size())
            ));
            reconstructed_sequence_iterator++;
         }
      }
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
   EVOBENCH_SCOPE(
      "ColumnEntryAppender", columnTypeToString(storage::column::SequenceColumn<Nucleotide>::TYPE)
   );
   auto* array =
      table_scan_node.getColumnTypeArrayBuilders<storage::column::SequenceColumn<Nucleotide>>().at(
         column_name
      );
   return appendSequences<Nucleotide>(table.columns.nuc_columns.at(column_name), row_ids, *array);
}

template <>
arrow::Status ColumnEntryAppender::operator()<storage::column::SequenceColumn<AminoAcid>>(
   ExecBatchBuilder& table_scan_node,
   const std::string& column_name,
   const storage::Table& table,
   const Bitmap& row_ids
) {
   EVOBENCH_SCOPE(
      "ColumnEntryAppender", columnTypeToString(storage::column::SequenceColumn<AminoAcid>::TYPE)
   );
   auto* array =
      table_scan_node.getColumnTypeArrayBuilders<storage::column::SequenceColumn<AminoAcid>>().at(
         column_name
      );
   return appendSequences<AminoAcid>(table.columns.aa_columns.at(column_name), row_ids, *array);
}

template <>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status ColumnEntryAppender::operator()<storage::column::ZstdCompressedStringColumn>(
   ExecBatchBuilder& table_scan_node,
   const std::string& column_name,
   const storage::Table& table,
   const Bitmap& row_ids
) {
   EVOBENCH_SCOPE(
      "ColumnEntryAppender", columnTypeToString(storage::column::ZstdCompressedStringColumn::TYPE)
   );
   auto* array =
      table_scan_node.getColumnTypeArrayBuilders<storage::column::ZstdCompressedStringColumn>().at(
         column_name
      );
   const auto& column =
      table.columns.getColumns<storage::column::ZstdCompressedStringColumn>().at(column_name);
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
   EVOBENCH_SCOPE("ColumnEntryAppender", columnTypeToString(Column::TYPE));
   auto& column = table.columns.getColumns<Column>().at(column_name);
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
            } else if constexpr (std::is_same_v<Column, storage::column::DictionaryEncodedColumn>) {
               auto value = column.getValueString(row_id);
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
      storage::column::visit(type, [&]<storage::column::Column Column>() {
         array_builders[type].emplace(name, std::make_shared<ArrowBuilder<Column>>());
      });
   }
}

arrow::Status ExecBatchBuilder::appendEntries(const storage::Table& table, const Bitmap& row_ids) {
   EVOBENCH_SCOPE("ExecBatchBuilder", "appendEntries");
   for (const auto& field : output_fields) {
      ARROW_RETURN_NOT_OK(storage::column::visit(
         field.type, ColumnEntryAppender{}, *this, field.name, table, row_ids
      ));
   }
   return arrow::Status::OK();
}

arrow::Result<arrow::ExecBatch> ExecBatchBuilder::finishBatch() {
   EVOBENCH_SCOPE("ExecBatchBuilder", "finishBatch");
   std::vector<arrow::Datum> data;
   for (auto& field : output_fields) {
      auto status = storage::column::visit(field.type, [&]<storage::column::Column Column>() {
         ARROW_ASSIGN_OR_RAISE(
            auto array, getColumnTypeArrayBuilders<Column>().at(field.name)->Finish()
         );
         data.push_back(array);
         return arrow::Status::OK();
      });
      ARROW_RETURN_NOT_OK(status);
   }
   return arrow::compute::ExecBatch::Make(data);
}

arrow::Result<std::optional<arrow::ExecBatch>> TableScanGenerator::produceNextBatch() {
   EVOBENCH_SCOPE("TableScanGenerator", "produceNextBatch");
   while (current_bitmap_reader.has_value()) {
      auto row_ids = current_bitmap_reader.value().nextBatch();
      if (row_ids.has_value()) {
         ARROW_RETURN_NOT_OK(
            exec_batch_builder.appendEntries(*table, Bitmap{std::move(row_ids.value())})
         );
         ARROW_ASSIGN_OR_RAISE(auto batch, exec_batch_builder.finishBatch());
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
   Bitmap bitmap_filter_,
   std::shared_ptr<const storage::Table> table,
   size_t batch_size_cutoff
) {
   const exec_node::TableScanGenerator generator(
      columns, std::move(bitmap_filter_), std::move(table), batch_size_cutoff
   );
   const arrow::acero::SourceNodeOptions source_node_options{
      exec_node::columnsToArrowSchema(columns),
      generator,
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", plan, {}, source_node_options);
}

}  // namespace rhydb::query_engine::exec_node
