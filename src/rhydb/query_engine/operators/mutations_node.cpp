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
#include "rhydb/query_engine/exec_node/schema_output_builder.h"
#include "rhydb/query_engine/operators/compute_filter.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

namespace {

using rhydb::storage::column::VerticalSequenceIndex;

template <typename SymbolType>
using SequenceDiffKey = typename VerticalSequenceIndex<SymbolType>::SequenceDiffKey;

template <typename SymbolType>
using SequenceDiff = typename VerticalSequenceIndex<SymbolType>::SequenceDiff;

__attribute__((noinline)) void initializeCountsWithSequenceCount(
   std::vector<uint32_t>& count_per_local_reference_position,
   uint32_t sequence_count
) {
   EVOBENCH_SCOPE("Mutations", "initializeCountsWithSequenceCount");
   // NOLINTNEXTLINE(modernize-loop-convert)
   for (uint32_t position_idx = 0; position_idx < count_per_local_reference_position.size();
        ++position_idx) {
      count_per_local_reference_position[position_idx] += sequence_count;
   }
}

__attribute__((noinline)) void subtractHorizontalBitmapCounts(
   std::vector<uint32_t>& count_per_local_reference_position,
   const std::map<uint32_t, roaring::Roaring>& horizontal_bitmaps
) {
   EVOBENCH_SCOPE("Mutations", "subtractHorizontalBitmapCounts");
   for (const auto& [_, n_bitmap] : horizontal_bitmaps) {
      for (const uint32_t position_idx : n_bitmap) {
         count_per_local_reference_position[position_idx] -= 1;
      }
   }
}

__attribute__((noinline)) void subtractCumulativeNsFromPositions(
   std::vector<uint32_t>& count_per_local_reference_position,
   uint32_t sequence_length,
   const std::vector<size_t>& cumulative_starts,
   const std::vector<size_t>& cumulative_ends
) {
   EVOBENCH_SCOPE("Mutations", "subtractCumulativeNsFromPositions");
   size_t running_total_start_n_offset = cumulative_starts.at(sequence_length);
   size_t start_position_iter = sequence_length - 1;
   while (true) {
      count_per_local_reference_position.at(start_position_iter) -= running_total_start_n_offset;
      running_total_start_n_offset += cumulative_starts.at(start_position_iter);
      if (start_position_iter == 0) {
         break;
      }
      start_position_iter -= 1;
   }
   size_t running_total_end_n_offset = cumulative_ends.at(0);
   size_t end_position_iter = 0;
   while (true) {
      count_per_local_reference_position.at(end_position_iter) -= running_total_end_n_offset;
      running_total_end_n_offset += cumulative_ends.at(end_position_iter + 1);
      if (end_position_iter == sequence_length - 1) {
         break;
      }
      end_position_iter += 1;
   }
}

__attribute__((noinline)) void subtractStartAndEndNCounts(
   std::vector<uint32_t>& count_per_local_reference_position,
   const storage::column::HorizontalCoverageIndex& coverage_index,
   size_t sequence_length
) {
   EVOBENCH_SCOPE("Mutations", "subtractStartAndEndNCounts");
   std::vector<size_t> cumulative_starts(sequence_length + 1);
   std::vector<size_t> cumulative_ends(sequence_length + 1);
   for (size_t chunk_id = 0; chunk_id < coverage_index.starts.size(); ++chunk_id) {
      const auto& chunk_starts = coverage_index.starts[chunk_id];
      const auto& chunk_ends = coverage_index.ends[chunk_id];
      for (size_t row_in_chunk = 0; row_in_chunk < chunk_starts.size(); ++row_in_chunk) {
         cumulative_starts.at(chunk_starts[row_in_chunk]) += 1;
         cumulative_ends.at(chunk_ends[row_in_chunk]) += 1;
      }
   }
   subtractCumulativeNsFromPositions(
      count_per_local_reference_position, sequence_length, cumulative_starts, cumulative_ends
   );
}

__attribute__((noinline)) void subtractFilteredNCounts(
   std::vector<uint32_t>& count_per_local_reference_position,
   const roaring::Roaring& filter,
   size_t sequence_length,
   const storage::column::HorizontalCoverageIndex& coverage_index
) {
   EVOBENCH_SCOPE("Mutations", "subtractFilteredNCounts");
   const auto& horizontal_bitmaps = coverage_index.horizontal_bitmaps;
   std::vector<size_t> cumulative_starts(sequence_length + 1);
   std::vector<size_t> cumulative_ends(sequence_length + 1);
   for (const uint32_t idx : filter) {
      auto iter = horizontal_bitmaps.find(idx);
      if (iter != horizontal_bitmaps.end()) {
         const roaring::Roaring& n_bitmap = iter->second;
         for (const size_t position_idx : n_bitmap) {
            count_per_local_reference_position[position_idx] -= 1;
         }
      }
      auto [start, end] = coverage_index.coverageRange(idx);
      cumulative_starts.at(start) += 1;
      cumulative_ends.at(end) += 1;
   }
   subtractCumulativeNsFromPositions(
      count_per_local_reference_position, sequence_length, cumulative_starts, cumulative_ends
   );
}

template <typename SymbolType>
void countActualMutations(
   SymbolMap<SymbolType, std::vector<uint32_t>>& count_of_mutations_per_position,
   std::vector<uint32_t>& count_per_local_reference_position,
   const std::map<SequenceDiffKey<SymbolType>, SequenceDiff<SymbolType>>& vertical_bitmaps
) {
   EVOBENCH_SCOPE("Mutations", "countActualMutations");
   for (const auto& [sequence_diff_key, sequence_diff] : vertical_bitmaps) {
      count_of_mutations_per_position[sequence_diff_key.symbol][sequence_diff_key.position] +=
         sequence_diff.getCardinality();
      count_per_local_reference_position[sequence_diff_key.position] -=
         sequence_diff.getCardinality();
   }
}

template <typename SymbolType>
void countActualFilteredMutations(
   SymbolMap<SymbolType, std::vector<uint32_t>>& count_of_mutations_per_position,
   std::vector<uint32_t>& count_per_local_reference_position,
   const roaring::Roaring& filter,
   const std::map<SequenceDiffKey<SymbolType>, SequenceDiff<SymbolType>>& vertical_bitmaps
) {
   EVOBENCH_SCOPE("Mutations", "countActualFilteredMutations");
   const auto& filter_roaring_array = filter.roaring.high_low_container;
   std::map<size_t, roaring::internal::container_t*> filter_containers;
   std::map<size_t, uint8_t> filter_container_typecodes;
   for (int32_t idx = 0; idx < filter_roaring_array.size; ++idx) {
      filter_containers[filter_roaring_array.keys[idx]] = filter_roaring_array.containers[idx];
      filter_container_typecodes[filter_roaring_array.keys[idx]] =
         filter_roaring_array.typecodes[idx];
   }

   for (const auto& [sequence_diff_key, sequence_diff] : vertical_bitmaps) {
      auto iter = filter_containers.find(sequence_diff_key.v_index);
      if (iter != filter_containers.end()) {
         auto filter_container = iter->second;
         const uint8_t filter_container_typecode =
            filter_container_typecodes.at(sequence_diff_key.v_index);

         auto contained_count = roaring::internal::container_and_cardinality(
            filter_container,
            filter_container_typecode,
            sequence_diff.rawContainer(),
            sequence_diff.getTypecode()
         );

         count_of_mutations_per_position[sequence_diff_key.symbol][sequence_diff_key.position] +=
            contained_count;
         count_per_local_reference_position[sequence_diff_key.position] -= contained_count;
      }
   }
}

template <typename SymbolType>
__attribute__((noinline)) void accumulateFinalCounts(
   const std::vector<uint32_t>& count_per_local_reference_position,
   const std::vector<typename SymbolType::Symbol>& local_reference,
   SymbolMap<SymbolType, std::vector<uint32_t>>& count_of_mutations_per_position
) {
   EVOBENCH_SCOPE("Mutations", "accumulateFinalCounts");
   for (size_t position_idx = 0; position_idx < count_per_local_reference_position.size();
        ++position_idx) {
      count_of_mutations_per_position[local_reference.at(position_idx)][position_idx] +=
         count_per_local_reference_position[position_idx];
   }
}

template <typename SymbolType>
void addMutationCountsForMixedBitmaps(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   const Bitmap& bitmap_filter,
   SymbolMap<SymbolType, std::vector<uint32_t>>& count_of_mutations_per_position
) {
   auto local_reference = sequence_column.getLocalReference();
   const size_t sequence_length = local_reference.size();
   std::vector<uint32_t> count_per_local_reference_position(sequence_length);

   // Terminal consumer of the filter's bitmap: the counting below reads its containers directly,
   // so materialize it once here.
   const roaring::Roaring filter_bitmap = bitmap_filter.toRoaring();

   initializeCountsWithSequenceCount(
      count_per_local_reference_position, filter_bitmap.cardinality()
   );
   subtractFilteredNCounts(
      count_per_local_reference_position,
      filter_bitmap,
      sequence_length,
      sequence_column.horizontal_coverage_index
   );
   countActualFilteredMutations(
      count_of_mutations_per_position,
      count_per_local_reference_position,
      filter_bitmap,
      sequence_column.vertical_sequence_index.vertical_bitmaps
   );
   accumulateFinalCounts(
      count_per_local_reference_position, local_reference, count_of_mutations_per_position
   );
}

template <typename SymbolType>
void addMutationCountsForFullBitmaps(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   SymbolMap<SymbolType, std::vector<uint32_t>>& count_of_mutations_per_position
) {
   auto local_reference = sequence_column.getLocalReference();
   const size_t sequence_length = local_reference.size();
   std::vector<uint32_t> count_per_local_reference_position(sequence_length);

   initializeCountsWithSequenceCount(
      count_per_local_reference_position, sequence_column.sequence_count
   );
   subtractHorizontalBitmapCounts(
      count_per_local_reference_position,
      sequence_column.horizontal_coverage_index.horizontal_bitmaps
   );
   subtractStartAndEndNCounts(
      count_per_local_reference_position, sequence_column.horizontal_coverage_index, sequence_length
   );
   countActualMutations(
      count_of_mutations_per_position,
      count_per_local_reference_position,
      sequence_column.vertical_sequence_index.vertical_bitmaps
   );
   accumulateFinalCounts(
      count_per_local_reference_position, local_reference, count_of_mutations_per_position
   );
}

template <typename SymbolType>
SymbolMap<SymbolType, std::vector<uint32_t>> calculateMutationsPerPosition(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   const Bitmap& bitmap_filter,
   uint64_t sequence_count_in_column
) {
   const size_t sequence_length = sequence_column.metadata->reference_sequence.size();
   SymbolMap<SymbolType, std::vector<uint32_t>> count_of_mutations_per_position;
   for (const auto symbol : SymbolType::SYMBOLS) {
      count_of_mutations_per_position[symbol] = std::vector<uint32_t>(sequence_length, 0);
   }
   const uint64_t filter_cardinality = bitmap_filter.cardinality();
   if (filter_cardinality == sequence_count_in_column) {
      addMutationCountsForFullBitmaps<SymbolType>(sequence_column, count_of_mutations_per_position);
   } else if (filter_cardinality > 0) {
      addMutationCountsForMixedBitmaps<SymbolType>(
         sequence_column, bitmap_filter, count_of_mutations_per_position
      );
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
   uint64_t sequence_count_in_column,
   exec_node::SchemaOutputBuilder& output_builder
) {
   const uint32_t sequence_length = sequence_column.metadata->reference_sequence.size();

   const SymbolMap<SymbolType, std::vector<uint32_t>> count_of_mutations_per_position =
      calculateMutationsPerPosition<SymbolType>(
         sequence_column, bitmap_filter, sequence_count_in_column
      );

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
                  MutationsNode<SymbolType>::MUTATION_FROM_FIELD_NAME,
                  [&]() -> OutputValue {
                     return {std::string(1, SymbolType::symbolToChar(symbol_in_reference_genome))};
                  }
               ));
               ARROW_RETURN_NOT_OK(output_builder.addValueIfContainedInOutput(
                  MutationsNode<SymbolType>::MUTATION_TO_FIELD_NAME,
                  [&]() -> OutputValue {
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
   auto bitmap_filter = computeFilter(filter, *table);

   auto table_handle = table;
   auto output_fields = getOutputSchema();
   auto sequence_column_identifiers = sequence_columns;
   const double given_min_proportion = min_proportion;
   std::function<arrow::Future<std::optional<arrow::ExecBatch>>()> producer =
      // NOLINTNEXTLINE(readability-function-cognitive-complexity)
      [table_handle,
       given_min_proportion,
       output_fields,
       bitmap_filter = std::move(bitmap_filter),
       sequence_column_identifiers,
       already_produced = false]() mutable -> arrow::Future<std::optional<arrow::ExecBatch>> {
      if (already_produced) {
         const std::optional<arrow::ExecBatch> result = std::nullopt;
         return arrow::Future{result};
      }
      already_produced = true;

      exec_node::SchemaOutputBuilder output_builder(output_fields);

      for (const auto& sequence_column_identifier : sequence_column_identifiers) {
         const storage::column::SequenceColumn<SymbolType>& sequence_column =
            table_handle->columns.template getColumns<storage::column::SequenceColumn<SymbolType>>()
               .at(sequence_column_identifier.name);

         ARROW_RETURN_NOT_OK(addMutationsToOutput<SymbolType>(
            sequence_column_identifier.name,
            sequence_column,
            given_min_proportion,
            bitmap_filter,
            table_handle->row_layout.numRows(),
            output_builder
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
