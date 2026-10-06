#pragma once

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"

namespace rhydb::query_engine::operators {
class FetchNode;
class MapNode;
class OrderByNode;
}  // namespace rhydb::query_engine::operators

namespace rhydb::query_engine::optimizer {

/// Optimization pass that moves a MapNode up the plan tree, so expensive per-row
/// computation (especially zstd decompression, which lives as a ScalarExpression inside
/// a MapNode) runs on as few rows as possible.
///
/// A MapNode is pulled up through a parent that reduces or reorders rows but does not
/// reference any column the MapNode produces:
///
/// ```
/// P(M(child))  →  M(P(child))
/// ```
///
/// One goal is pulling a MapNode above a FetchNode (limit/offset)
/// so a `limit` no longer forces every row to be decompressed and then discarded.
///
/// The pass also contracts two directly stacked MapNodes into one:
///
/// ```
/// M_upper(M_lower(child))  →  M_merged(child)
/// ```
///
/// by inlining each lower assignment into the upper expressions that reference it (substituting the
/// referencing `FieldRef` with the lower expression). This turns the decompress + `at` pattern
/// (`at(x) := main.at(pos)` over `main := zstdDecompress(main)`) into a single map holding the
/// nested `at(zstdDecompress(main))`, which downstream passes can match as one clear expression
/// tree.
class MapPullupPass : public PipelinePassBase<MapPullupPass> {
  public:
   using PipelinePassBase<MapPullupPass>::operator();

   // Shadowing the PipelinePassBase defaults is the intended way to customize a pass.
   // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)
   operators::QueryNodePtr operator()(operators::FetchNode& node);
   operators::QueryNodePtr operator()(operators::OrderByNode& node);
   operators::QueryNodePtr operator()(operators::MapNode& node);
   // NOLINTEND(bugprone-derived-method-shadowing-base-method)
};

}  // namespace rhydb::query_engine::optimizer
