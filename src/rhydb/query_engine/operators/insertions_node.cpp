#include "rhydb/query_engine/operators/insertions_node.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <spdlog/spdlog.h>
#include <boost/container_hash/hash.hpp>
#include <nlohmann/json.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/schema_output_builder.h"
#include "rhydb/query_engine/operators/compute_filter.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/insertion_index.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {
struct PositionAndInsertionKey {
   uint32_t position_idx;
   std::string_view insertion_value;

   bool operator==(const PositionAndInsertionKey& other) const {
      return position_idx == other.position_idx && insertion_value == other.insertion_value;
   }
};
}  // namespace rhydb::query_engine::operators

template <>
struct std::hash<rhydb::query_engine::operators::PositionAndInsertionKey> {
   std::size_t operator()(const rhydb::query_engine::operators::PositionAndInsertionKey& key
   ) const noexcept {
      std::size_t seed = 0;
      boost::hash_combine(seed, key.position_idx);
      boost::hash_combine(seed, key.insertion_value);
      return seed;
   }
};

namespace rhydb::query_engine::operators {

namespace {

template <typename SymbolType>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status addAggregatedInsertionsToInsertionCounts(
   const std::string& sequence_name,
   const Bitmap& bitmap_filter,
   const storage::Table& table,
   exec_node::SchemaOutputBuilder& output_builder
) {
   const auto& sequence_column =
      table.columns.getColumns<storage::column::SequenceColumn<SymbolType>>().at(sequence_name);
   std::unordered_map<PositionAndInsertionKey, uint32_t> all_insertions;
   const roaring::Roaring filter_bitmap = bitmap_filter.toRoaring();
   auto bitmap_cardinality = filter_bitmap.cardinality();
   if (bitmap_cardinality == 0) {
      return arrow::Status::OK();
   }
   if (bitmap_cardinality == table.row_layout.numRows()) {
      for (const auto& [position, insertions_at_position] :
           sequence_column.insertion_index.getInsertionPositions()) {
         for (const auto& insertion : insertions_at_position.insertions) {
            all_insertions[PositionAndInsertionKey{position, insertion.value}] +=
               insertion.row_ids.cardinality();
         }
      }
   } else {
      for (const auto& [position, insertions_at_position] :
           sequence_column.insertion_index.getInsertionPositions()) {
         for (const auto& insertion : insertions_at_position.insertions) {
            const uint32_t count = insertion.row_ids.and_cardinality(filter_bitmap);
            if (count > 0) {
               all_insertions[PositionAndInsertionKey{position, insertion.value}] += count;
            }
         }
      }
   }
   for (const auto& [position_and_insertion, count] : all_insertions) {
      using OutputValue = std::optional<std::variant<std::string, bool, int32_t, double>>;
      ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
         InsertionsNode<SymbolType>::POSITION_FIELD_NAME,
         [&]() -> OutputValue { return static_cast<int32_t>(position_and_insertion.position_idx); }
      ));
      ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
         InsertionsNode<SymbolType>::INSERTED_SYMBOLS_FIELD_NAME,
         [&]() -> OutputValue { return std::string(position_and_insertion.insertion_value); }
      ));
      ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
         InsertionsNode<SymbolType>::SEQUENCE_FIELD_NAME,
         [&]() -> OutputValue { return sequence_name; }
      ));
      ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
         InsertionsNode<SymbolType>::COUNT_FIELD_NAME,
         [&]() -> OutputValue { return static_cast<int32_t>(count); }
      ));
   }
   return arrow::Status::OK();
}

}  // namespace

template <typename SymbolType>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<arrow::acero::ExecNode*> InsertionsNode<SymbolType>::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& /*query_options*/
) const {
   auto bitmap_filter = computeFilter(filter, *table);

   auto table_handle = table;
   auto sequence_columns_handle = sequence_columns;
   auto output_fields = getOutputSchema();
   std::function<arrow::Future<std::optional<arrow::ExecBatch>>()> producer =
      // NOLINTNEXTLINE(readability-function-cognitive-complexity)
      [table_handle,
       output_fields,
       bitmap_filter,
       sequence_columns_handle,
       already_produced = false]() mutable -> arrow::Future<std::optional<arrow::ExecBatch>> {
      if (already_produced) {
         const std::optional<arrow::ExecBatch> result = std::nullopt;
         return arrow::Future{result};
      }
      already_produced = true;

      exec_node::SchemaOutputBuilder output_builder{output_fields};

      for (const auto& [sequence_name, _] : sequence_columns_handle) {
         ARROW_RETURN_NOT_OK(addAggregatedInsertionsToInsertionCounts<SymbolType>(
            sequence_name, bitmap_filter, *table_handle, output_builder
         ));
      }

      ARROW_ASSIGN_OR_RAISE(
         const std::vector<arrow::Datum> result_columns, output_builder.finish()
      );
      ARROW_ASSIGN_OR_RAISE(
         const std::optional<arrow::ExecBatch> result, arrow::ExecBatch::Make(result_columns)
      );
      return arrow::Future{result};
   };

   const arrow::acero::SourceNodeOptions options{
      exec_node::columnsToArrowSchema(output_fields),
      std::move(producer),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", &plan, {}, options);
}

template <typename SymbolType>
nlohmann::json InsertionsNode<SymbolType>::toJson() const {
   nlohmann::json sequence_names_json = nlohmann::json::array();
   for (const auto& sequence_column : sequence_columns) {
      sequence_names_json.push_back(sequence_column.name);
   }
   return {
      {"type", nodeKindToString(kind())},
      {"table", table->logTable()},
      {"filter", filter->toString()},
      {"sequenceColumns", std::move(sequence_names_json)},
   };
}

template class InsertionsNode<Nucleotide>;
template class InsertionsNode<AminoAcid>;

}  // namespace rhydb::query_engine::operators
