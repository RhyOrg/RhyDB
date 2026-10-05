#pragma once

#include <concepts>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::operators {
class FilterNode;
class MapNode;
class ProjectNode;
class OrderByNode;
class TableScanNode;
class UnionAllNode;
class JoinNode;
}  // namespace rhydb::query_engine::operators

namespace rhydb::query_engine::optimizer {

/// Pushes filters as deep into the plan as is semantics-preserving.
class FilterPushdownPass : public PipelinePassBase<FilterPushdownPass> {
   std::vector<std::unique_ptr<scalar_expressions::ScalarExpression>> current_filters;

   /// Adds a filter to `current_filters`, flattening a top-level `And` into its conjuncts so each
   /// can be pushed independently.
   void addFilter(std::unique_ptr<scalar_expressions::ScalarExpression> filter);

   /// Barrier body shared by every barrier handler: processes `child` with a fresh pass so filters
   /// inside the child subtree are pushed down, without letting filters from above the barrier leak
   /// into it.
   static void barrier(operators::QueryNodePtr& child);

  public:
   // Shadowing the PipelinePassBase defaults is the intended way to customize a pass.
   // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)
   /// Visits `node`, then wraps any filters it left pending into a FilterNode above it. This is
   /// what makes "break pushdown" the default.
   void propagateToNode(operators::QueryNodePtr& node);

   // Transparent: push filters down into the child (row set and referenced columns unchanged).
   operators::QueryNodePtr operator()(operators::ProjectNode& node);
   operators::QueryNodePtr operator()(operators::OrderByNode& node);

   // Consumes the filters into the scan's own filter field (the bitmap pre-filter).
   operators::QueryNodePtr operator()(operators::TableScanNode& node);

   // Bespoke handling.
   operators::QueryNodePtr operator()(operators::FilterNode& node);
   operators::QueryNodePtr operator()(operators::MapNode& node);
   operators::QueryNodePtr operator()(operators::UnionAllNode& node);
   operators::QueryNodePtr operator()(operators::JoinNode& node);
   // NOLINTEND(bugprone-derived-method-shadowing-base-method)

   // Fail-closed default. A node with no explicit handler and a single `QueryNodePtr child` is a
   // barrier: the filter above it is retained by `propagateToNode`, while its child subtree is
   // pushed into with a fresh pass. Any other (childless/leaf, or resolved) node is
   // left untouched.
   template <typename T>
   // NOLINTNEXTLINE(misc-no-recursion)
   operators::QueryNodePtr operator()(T& node) {
      if constexpr (requires {
                       { node.child } -> std::same_as<operators::QueryNodePtr&>;
                    }) {
         barrier(node.child);
      }
      return nullptr;
   }
};

}  // namespace rhydb::query_engine::optimizer
