#include "rhydb/query_engine/operators/mutations_node.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <nlohmann/json.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/symbol_map.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/async_batch_producer.h"
#include "rhydb/query_engine/exec_node/schema_output_builder.h"
#include "rhydb/query_engine/operators/compute_filter.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

namespace {

/// The number of filtered rows carrying each symbol at each position, computed chunk by chunk from
/// the column's indexes (see `storage::sequence::addSymbolCounts`). Rows that do not cover a
/// position are not counted there.
template <typename SymbolType>
SymbolMap<SymbolType, std::vector<uint32_t>> calculateMutationsPerPosition(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   const Bitmap& bitmap_filter
) {
   EVOBENCH_SCOPE("Mutations", "calculateMutationsPerPosition");
   const size_t sequence_length = sequence_column.sequenceLength();
   storage::sequence::SymbolCounts counts{static_cast<uint8_t>(SymbolType::COUNT), sequence_length};
   for (const auto& [chunk_id, rows] : bitmap_filter) {
      storage::sequence::addSymbolCounts(sequence_column.chunk(chunk_id), rows, counts);
   }

   SymbolMap<SymbolType, std::vector<uint32_t>> count_of_mutations_per_position;
   for (const auto symbol : SymbolType::SYMBOLS) {
      auto& symbol_counts = count_of_mutations_per_position[symbol];
      symbol_counts.resize(sequence_length);
      for (uint32_t position = 0; position < sequence_length; ++position) {
         symbol_counts[position] = counts.at(static_cast<uint8_t>(symbol), position);
      }
   }
   return count_of_mutations_per_position;
}

template <typename SymbolType>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status addMutationsToOutput(
   const std::string& sequence_name,
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   double min_proportion,
   const Bitmap& bitmap_filter,
   exec_node::SchemaOutputBuilder& output_builder
) {
   const uint32_t sequence_length = sequence_column.metadata->reference_sequence.size();

   const SymbolMap<SymbolType, std::vector<uint32_t>> count_of_mutations_per_position =
      calculateMutationsPerPosition<SymbolType>(sequence_column, bitmap_filter);

   for (uint32_t pos = 0; pos < sequence_length; ++pos) {
      uint32_t total = 0;
      for (const typename SymbolType::Symbol symbol : SymbolType::VALID_MUTATION_SYMBOLS) {
         total += count_of_mutations_per_position.at(symbol)[pos];
      }
      if (total == 0) {
         continue;
      }
      const auto threshold_count =
         min_proportion == 0
            ? 0
            : static_cast<uint32_t>(std::ceil(static_cast<double>(total) * min_proportion) - 1);

      const typename SymbolType::Symbol symbol_in_reference_genome =
         sequence_column.metadata->reference_sequence.at(pos);

      for (const auto symbol : SymbolType::VALID_MUTATION_SYMBOLS) {
         if (symbol_in_reference_genome != symbol) {
            const uint32_t count = count_of_mutations_per_position.at(symbol)[pos];
            if (count > threshold_count) {
               const double proportion = static_cast<double>(count) / static_cast<double>(total);
               using OutputValue = std::optional<std::variant<std::string, bool, int32_t, double>>;
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::MUTATION_FROM_FIELD_NAME, [&]() -> OutputValue {
                     return {std::string(1, SymbolType::symbolToChar(symbol_in_reference_genome))};
                  }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::MUTATION_TO_FIELD_NAME, [&]() -> OutputValue {
                     return {std::string(1, SymbolType::symbolToChar(symbol))};
                  }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::POSITION_FIELD_NAME,
                  [&]() -> OutputValue { return {static_cast<int32_t>(pos + 1)}; }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::SEQUENCE_FIELD_NAME,
                  [&]() -> OutputValue { return {sequence_name}; }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::PROPORTION_FIELD_NAME,
                  [&]() -> OutputValue { return {proportion}; }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::COUNT_FIELD_NAME,
                  [&]() -> OutputValue { return {static_cast<int32_t>(count)}; }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::COVERAGE_FIELD_NAME,
                  [&]() -> OutputValue { return {static_cast<int32_t>(total)}; }
               ));
            }
         }
      }
   }
   return arrow::Status::OK();
}

}  // namespace

template <typename SymbolType>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<arrow::acero::ExecNode*> MutationsNode<SymbolType>::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& /*query_options*/
) const {
   auto compiled_filter = compileFilter(filter, table);

   auto table_handle = table;
   auto output_fields = getOutputSchema();
   auto sequence_column_identifiers = sequence_columns;
   const double given_min_proportion = min_proportion;
   exec_node::BatchProducer producer =
      // NOLINTNEXTLINE(readability-function-cognitive-complexity)
      [table_handle,
       given_min_proportion,
       output_fields,
       compiled_filter,
       sequence_column_identifiers,
       already_produced = false] mutable -> arrow::Result<std::optional<arrow::ExecBatch>> {
      if (already_produced) {
         return std::nullopt;
      }
      already_produced = true;

      const Bitmap bitmap_filter = compiled_filter->evaluate();

      exec_node::SchemaOutputBuilder output_builder(output_fields);

      for (const auto& sequence_column_identifier : sequence_column_identifiers) {
         const storage::column::SequenceColumn<SymbolType>& sequence_column =
            table_handle->template getColumn<storage::column::SequenceColumn<SymbolType>>(
               sequence_column_identifier.name
            );

         ARROW_RETURN_NOT_OK(
            addMutationsToOutput<SymbolType>(
               sequence_column_identifier.name,
               sequence_column,
               given_min_proportion,
               bitmap_filter,
               output_builder
            )
         );
      }
      ARROW_ASSIGN_OR_RAISE(
         const std::vector<arrow::Datum> result_columns, output_builder.finish()
      );
      ARROW_ASSIGN_OR_RAISE(
         std::optional<arrow::ExecBatch> result, arrow::ExecBatch::Make(result_columns)
      );
      return result;
   };

   const arrow::acero::SourceNodeOptions options{
      exec_node::columnsToArrowSchema(output_fields),
      exec_node::makeAsyncBatchGenerator(plan, std::move(producer)),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", &plan, {}, options);
}

template <typename SymbolType>
nlohmann::json MutationsNode<SymbolType>::toJson() const {
   nlohmann::json sequence_columns_json = nlohmann::json::array();
   for (const auto& sequence_column : sequence_columns) {
      sequence_columns_json.push_back(sequence_column.name);
   }
   nlohmann::json fields_json = nlohmann::json::array();
   for (const auto& field : fields) {
      fields_json.push_back(std::string{field});
   }
   return {
      {"type", nodeKindToString(kind())},
      {"table", table->logTable()},
      {"filter", filter->toString()},
      {"sequenceColumns", std::move(sequence_columns_json)},
      {"minProportion", min_proportion},
      {"fields", std::move(fields_json)},
   };
}

template class MutationsNode<Nucleotide>;
template class MutationsNode<AminoAcid>;

}  // namespace rhydb::query_engine::operators
