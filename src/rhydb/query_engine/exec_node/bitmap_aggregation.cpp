#include "rhydb/query_engine/exec_node/bitmap_aggregation.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/compute/api.h>
#include <arrow/compute/exec.h>
#include <arrow/compute/ordering.h>
#include <arrow/datum.h>
#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/util/future.h>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/dimension_index.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::exec_node {

namespace {

using roaring_util::RoaringContainerView;

/// One observed combination across the grouping dimensions together with the number of (filtered)
/// rows carrying it. Each entry of `group_indices` indexes into the corresponding dimension's
/// group labels, i.e. it identifies which group was chosen in that dimension. Keeping indices
/// rather than the values themselves makes the recursion type-agnostic: the actual value is
/// resolved per dimension only when the output is materialized.
struct GroupCombination {
   std::vector<size_t> group_indices;
   uint64_t count;
};

/// Hashes a group-index tuple so combination counts can accumulate in an `unordered_map`
struct GroupIndicesHash {
   size_t operator()(const std::vector<size_t>& indices) const {
      size_t seed = indices.size();
      for (const size_t index : indices) {
         // boost-style hash_combine
         seed ^= index + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
      }
      return seed;
   }
};

/// The per-dimension groups are combined and then the count per group stored in this data structure
using CombinationCounts = std::unordered_map<std::vector<size_t>, uint64_t, GroupIndicesHash>;

/// Recursively intersect one chunk's per-dimension group containers, depth by depth, and add the
/// cardinality of each surviving full combination to `counts`. `current` is the running
/// container-level intersection of the groups chosen so far; it is seeded with the filter chunk (so
/// the not-yet-filtered group views are bounded by the filter here) and is only ever a view or a
/// short-lived owned temporary, so no `Bitmap` is built and no group container is
/// copied. Empty intersections are pruned. A dimension whose groups collapsed to a single
/// whole-chunk label contributes no intersection at all -- the running set passes through unchanged
/// with that label fixed.
// NOLINTNEXTLINE(misc-no-recursion)
void aggregateChunk(
   size_t depth,
   const roaring::internal::container_t* current,
   uint8_t current_typecode,
   const std::vector<const DimensionIndexChunk*>& groups_by_dimension,
   std::vector<size_t>& chosen_indices,
   CombinationCounts& counts
) {
   const size_t last_dimension = groups_by_dimension.size() - 1;

   if (const size_t* group_index =
          std::get_if<SingletonValueDimension>(groups_by_dimension[depth])) {
      // Every row of the running intersection carries this one label; nothing to intersect.
      chosen_indices[depth] = *group_index;
      if (depth == last_dimension) {
         const auto count = static_cast<uint64_t>(
            roaring::internal::container_get_cardinality(current, current_typecode)
         );
         if (count > 0) {
            counts[chosen_indices] += count;
         }
      } else {
         aggregateChunk(
            depth + 1, current, current_typecode, groups_by_dimension, chosen_indices, counts
         );
      }
      return;
   }

   for (const auto& [group_index, group] :
        std::get<ValueBitmapDimension>(*groups_by_dimension[depth])) {
      chosen_indices[depth] = group_index;
      const RoaringContainerView view = group.view();

      if (depth == last_dimension) {
         // Leaf: only the cardinality of the final intersection is needed, so compute it directly
         // without allocating a result container.
         const auto count = static_cast<uint64_t>(roaring::internal::container_and_cardinality(
            current, current_typecode, view.rawContainer(), view.getTypecode()
         ));
         if (count > 0) {
            counts[chosen_indices] += count;
         }
         continue;
      }

      uint8_t result_typecode = 0;
      auto* intersection = roaring::internal::container_and(
         current, current_typecode, view.rawContainer(), view.getTypecode(), &result_typecode
      );
      if (roaring::internal::container_nonzero_cardinality(intersection, result_typecode)) {
         aggregateChunk(
            depth + 1, intersection, result_typecode, groups_by_dimension, chosen_indices, counts
         );
      }
      roaring::internal::container_free(intersection, result_typecode);
   }
}

/// Computes the aggregation groups counts one 2^16 chunk at a time. It does so by enumerating all
/// combination of the per-dimension groups. The combination's cardinalities are computed
/// efficiently using bitmap intersection
std::vector<GroupCombination> computeCombinations(
   const std::vector<std::unique_ptr<DimensionIndex>>& indexes,
   const Bitmap& filter_bitmap
) {
   const size_t num_dimensions = indexes.size();
   if (num_dimensions == 0) {
      return {};
   }

   // Counts keyed by the group-index tuple, accumulated in an unordered_map for O(1) updates; the
   // result is sorted back into ascending (lexicographic-by-index) order below, which -- because
   // every dimension numbers its groups in output order -- is the order the result rows appear in.
   CombinationCounts counts;
   std::vector<size_t> chosen_indices(num_dimensions);

   for (const auto& [chunk_id, filter_view] : filter_bitmap) {
      // Build each dimension's groups for this chunk; each group carries its own rows (a
      // CopyOnWriteContainer that borrows stored bitmaps or owns computed ones), so nothing
      // external needs to outlive the aggregation. A filter container is never empty, and each
      // dimension's groups partition its rows, so every dimension yields at least one group; a
      // dimension that (defensively) produced none would simply contribute no combinations in
      // `aggregateChunk`.
      std::vector<DimensionIndexChunk> groups(num_dimensions);
      std::vector<const DimensionIndexChunk*> groups_at_chunk(num_dimensions);
      for (size_t dimension = 0; dimension < num_dimensions; ++dimension) {
         groups[dimension] = indexes[dimension]->chunk(chunk_id, filter_view);
         groups_at_chunk[dimension] = &groups[dimension];
      }
      aggregateChunk(
         0,
         filter_view.rawContainer(),
         filter_view.getTypecode(),
         groups_at_chunk,
         chosen_indices,
         counts
      );
   }

   std::vector<GroupCombination> combinations;
   combinations.reserve(counts.size());
   while (!counts.empty()) {
      auto node = counts.extract(counts.begin());
      combinations.push_back(
         GroupCombination{.group_indices = std::move(node.key()), .count = node.mapped()}
      );
   }
   // Restore the ascending group-index-tuple order the unordered_map does not keep.
   std::ranges::sort(combinations, std::less{}, &GroupCombination::group_indices);
   return combinations;
}

/// Materializes one chunk of this operator's output: the group keys and aggregation output (count)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<arrow::ExecBatch> buildBatch(
   const std::vector<GroupCombination>& combinations,
   const std::vector<std::shared_ptr<arrow::Array>>& values_per_dimension,
   size_t dimension_count,
   size_t begin,
   size_t end
) {
   std::vector<arrow::Datum> result_columns;
   result_columns.reserve(dimension_count + 1);

   for (size_t i = 0; i < dimension_count; ++i) {
      // The group indices of this dimension for the combinations in [begin, end), as an int32 array
      // to Take the value array with. Distinct group counts fit comfortably in int32.
      arrow::Int32Builder index_builder;
      ARROW_RETURN_NOT_OK(index_builder.Reserve(static_cast<int64_t>(end - begin)));
      for (size_t combination_idx = begin; combination_idx < end; ++combination_idx) {
         index_builder.UnsafeAppend(
            static_cast<int32_t>(combinations[combination_idx].group_indices[i])
         );
      }
      std::shared_ptr<arrow::Array> indices;
      ARROW_RETURN_NOT_OK(index_builder.Finish(&indices));
      arrow::Datum gathered;
      ARROW_ASSIGN_OR_RAISE(gathered, arrow::compute::Take(values_per_dimension[i], indices));
      result_columns.push_back(std::move(gathered));
   }

   arrow::Int64Builder count_builder;
   ARROW_RETURN_NOT_OK(count_builder.Reserve(static_cast<int64_t>(end - begin)));
   for (size_t combination_idx = begin; combination_idx < end; ++combination_idx) {
      count_builder.UnsafeAppend(static_cast<int64_t>(combinations[combination_idx].count));
   }
   arrow::Datum count_datum;
   ARROW_ASSIGN_OR_RAISE(count_datum, count_builder.Finish());
   result_columns.push_back(std::move(count_datum));

   return arrow::ExecBatch::Make(result_columns);
}

/// Produces the batches of a bitmap aggregation for an Acero source node. The first pull resolves
/// every dimension into its index and counts all combinations; that and every later pull then
/// emits the next batch of at most `batch_size` combinations, so the result is built only as the
/// downstream pulls it rather than materialized at once (the number of combinations is bounded
/// only by the filtered row count, so holding the whole result could blow up peak memory).
class BitmapAggregationGenerator {
  public:
   BitmapAggregationGenerator(
      std::shared_ptr<const storage::Table> table,
      Bitmap filter,
      std::vector<ResolvedGroupingDimension> dimensions,
      size_t batch_size
   )
       : table(std::move(table)),
         filter(std::move(filter)),
         dimensions(std::move(dimensions)),
         batch_size(batch_size) {
      RHYDB_ASSERT(batch_size > 0);
   }

   // By arrow specification, this function will not be called re-entrantly
   arrow::Future<std::optional<arrow::ExecBatch>> operator()() {
      try {
         return nextBatch();
      } catch (const std::exception& exception) {
         return arrow::Status::ExecutionError(exception.what());
      }
   }

  private:
   /// Builds every dimension's index and counts all combinations. Afterward only the combinations
   /// and the per-dimension value arrays are kept; the indexes and the filter are released.
   arrow::Status aggregate() {
      std::vector<std::unique_ptr<DimensionIndex>> indexes;
      indexes.reserve(dimensions.size());
      for (const auto& dimension : dimensions) {
         ARROW_ASSIGN_OR_RAISE(auto index, buildDimensionIndex(dimension, *table, filter));
         indexes.push_back(std::move(index));
      }

      combinations = computeCombinations(indexes, filter);

      // Element `group_index` of a dimension's value array is that group's typed value
      values_per_dimension.reserve(indexes.size());
      for (const auto& index : indexes) {
         ARROW_ASSIGN_OR_RAISE(auto values, index->values());
         values_per_dimension.push_back(std::move(values));
      }
      filter = Bitmap{};
      return arrow::Status::OK();
   }

   arrow::Result<std::optional<arrow::ExecBatch>> nextBatch() {
      if (!aggregated) {
         ARROW_RETURN_NOT_OK(aggregate());
         aggregated = true;
      }
      if (next_combination >= combinations.size()) {
         return std::nullopt;
      }
      const size_t begin = next_combination;
      const size_t end = std::min(begin + batch_size, combinations.size());
      ARROW_ASSIGN_OR_RAISE(
         arrow::ExecBatch batch,
         buildBatch(combinations, values_per_dimension, dimensions.size(), begin, end)
      );
      next_combination = end;
      return batch;
   }

   std::shared_ptr<const storage::Table> table;
   Bitmap filter;
   std::vector<ResolvedGroupingDimension> dimensions;
   size_t batch_size;

   bool aggregated = false;
   std::vector<GroupCombination> combinations;
   std::vector<std::shared_ptr<arrow::Array>> values_per_dimension;
   size_t next_combination = 0;
};

}  // namespace

arrow::Result<arrow::acero::ExecNode*> addBitmapAggregationNode(
   arrow::acero::ExecPlan& plan,
   std::shared_ptr<const storage::Table> table,
   Bitmap filter,
   std::vector<ResolvedGroupingDimension> dimensions,
   std::shared_ptr<arrow::Schema> output_schema,
   size_t batch_size
) {
   RHYDB_ASSERT_EQ(dimensions.size() + 1, static_cast<size_t>(output_schema->num_fields()));
   // Shared, because the source node copies its generator, which would copy the filter bitmap
   auto generator = std::make_shared<BitmapAggregationGenerator>(
      std::move(table), std::move(filter), std::move(dimensions), batch_size
   );
   const arrow::acero::SourceNodeOptions options{
      std::move(output_schema),
      [generator] { return (*generator)(); },
      arrow::Ordering::Implicit(),
   };
   ARROW_ASSIGN_OR_RAISE(auto* node, arrow::acero::MakeExecNode("source", &plan, {}, options));
   node->SetLabel("bitmap_aggregation");
   return node;
}

}  // namespace rhydb::query_engine::exec_node
