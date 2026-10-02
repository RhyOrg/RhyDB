#pragma once

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"

namespace rhydb::query_engine::operators {
class AggregateNode;
}  // namespace rhydb::query_engine::operators

namespace rhydb::query_engine::optimizer {

/// Optimization pass that recognizes a `groupBy` with a single bare `count()` (no source column)
/// and turns it into a dedicated, far cheaper node:
///
///   * With no grouping keys (a full `count(*)`) the result is just the filter's cardinality, so
///     the aggregate is replaced by a `CountFilterNode` that reads it straight off the scan's
///     filter bitmap -- no rows are materialized at all. This is what `default.count()` and
///     `default.groupBy({n := count()})` compile to.
///
///   * With grouping keys that can be computed directly from roaring bitmaps, the aggregate is
///     replaced by a `BitmapAggregationNode`.
///
/// A `count(<column>)` (a count carrying a source column) is not the bare count this pass handles
/// and is always left untouched.
///
/// Each grouping key must be one of:
///   * a sequence-position lookup produced by an `At` assignment of a directly preceding `map`
///     (the mutation co-occurrence pattern), e.g.
///
///         ... | map({symbol_123 := main.at(123)}) | groupBy({count := count()}, {symbol_123})
///
///   * a column carrying a per-value bitmap index, read straight from the table scan, e.g.
///
///         ... | groupBy({count := count()}, {division})
///
///     Today that is only indexed string columns; any column exposing a per-value inverted index
///     (e.g. bool, or dates once they gain one) could be added the same way.
///
/// The two may be mixed within one `groupBy`. In query-node terms this is an `AggregateNode` whose
/// only aggregate is `count()` and all of whose grouping keys resolve, against the leaf table scan,
/// to either an `At`-derived sequence position or an indexed string column. Such a node is replaced
/// by a `BitmapAggregationNode`, which computes the grouping directly from the per-value roaring
/// bitmaps instead of materializing one row per sequence and hashing it. Queries that don't match
/// this shape are left untouched, so the generic map/groupBy execution still handles every other
/// case.
///
/// This pass runs after FilterPushdownPass so the matched pipeline's leaf has already been
/// collapsed into a single `TableScanNode` carrying the full filter, which the rewrite reads to
/// resolve each grouping key against the table schema. It also runs after MapPullupPass, so at most
/// one `MapNode` sits between the aggregate and the scan. Traversal into every other node is
/// provided by PipelinePassBase; only `AggregateNode` needs custom handling.
class BitmapAggregationRewritePass : public PipelinePassBase<BitmapAggregationRewritePass> {
  public:
   using PipelinePassBase<BitmapAggregationRewritePass>::operator();

   operators::QueryNodePtr operator()(operators::AggregateNode& node);
};

}  // namespace rhydb::query_engine::optimizer
