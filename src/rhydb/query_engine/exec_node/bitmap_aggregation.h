#pragma once

#include <memory>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/result.h>
#include <arrow/type_fwd.h>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/exec_node/dimension_index.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::exec_node {

/// Adds a bitmap-aggregation source node to `plan` and returns it.
///
/// The node (an Acero source node) groups the rows of `filter` by `dimensions`, emitting one row
/// per observed combination of their values together with the number of rows carrying it, in
/// ascending group order. `output_schema` holds one column per dimension followed by the INT64
/// count column.
///
/// The counts are computed by recursively partitioning the filtered row-set with the per-dimension,
/// per-value roaring bitmaps, one 2^16 chunk at a time, pruning empty combinations. Only non-empty
/// combinations are visited (their number is bounded by the count of matching rows), so this
/// scales to many dimensions without the exponential blow-up of a full Cartesian product. The
/// counting runs when the node first produces, and the result is emitted in batches of at most
/// `batch_size` rows, each built only when the downstream pulls it.
arrow::Result<arrow::acero::ExecNode*> addBitmapAggregationNode(
   arrow::acero::ExecPlan& plan,
   std::shared_ptr<const storage::Table> table,
   Bitmap filter,
   std::vector<ResolvedGroupingDimension> dimensions,
   std::shared_ptr<arrow::Schema> output_schema,
   size_t batch_size
);

}  // namespace rhydb::query_engine::exec_node
