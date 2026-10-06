#include "rhydb/query_engine/optimizer/filter_pushdown_pass.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "rhydb/query_engine/operator_visitor.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/join_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/union_all_node.h"
#include "rhydb/query_engine/scalar_expressions/and.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"

using rhydb::query_engine::scalar_expressions::And;

namespace rhydb::query_engine::optimizer {

namespace {

bool isFieldRef(const operators::MapNode::Assignment& assignment) {
   const scalar_expressions::ScalarExpression* expression = assignment.expression.get();
   // Sequence columns cannot be read into an arrow plan directly: they are stored zstd-compressed
   // and decompressed on demand, so as a hack we treat a zstd-decompression as an identity here.
   // A filter referencing such a column is then pushed down and compiles against the physical
   // (compressed) column by name. This may produce misleading error messages in edge cases.
   while (const auto* decompress =
             scalar_expressions::dynCast<scalar_expressions::ZstdDecompressScalar>(expression)) {
      expression = decompress->input.get();
   }
   const auto* field_ref = scalar_expressions::dynCast<scalar_expressions::FieldRef>(expression);
   return field_ref != nullptr && field_ref->column.name == assignment.output_column.name;
}

}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
void FilterPushdownPass::propagateToNode(operators::QueryNodePtr& node) {
   if (auto replacement = operators::visit(*node, *this)) {
      node = std::move(replacement);
   }
   if (!current_filters.empty()) {
      auto remaining_filter = std::make_unique<And>(std::move(current_filters));
      current_filters.clear();
      node = std::make_unique<operators::FilterNode>(std::move(node), std::move(remaining_filter));
   }
}

// NOLINTNEXTLINE(misc-no-recursion)
void FilterPushdownPass::addFilter(std::unique_ptr<scalar_expressions::ScalarExpression> filter) {
   // Split a top-level conjunction into its conjuncts so each can be pushed independently, e.g. a
   // hasMutation() conjunct into the scan while a conjunct on a map-produced column stays above the
   // map. Nested conjunctions are flattened recursively.
   if (auto* and_expression = scalar_expressions::dynCast<And>(filter.get())) {
      for (auto& conjunct : and_expression->takeChildren()) {
         addFilter(std::move(conjunct));
      }
      return;
   }
   current_filters.push_back(std::move(filter));
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr FilterPushdownPass::operator()(operators::FilterNode& node) {
   addFilter(std::move(node.filter));
   auto child = std::move(node.child);
   propagateToNode(child);
   return child;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr FilterPushdownPass::operator()(operators::ProjectNode& node) {
   propagateToNode(node.child);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr FilterPushdownPass::operator()(operators::OrderByNode& node) {
   if (node.randomize_seed.has_value()) {
      barrier(node.child);
   } else {
      propagateToNode(node.child);
   }
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr FilterPushdownPass::operator()(operators::MapNode& node) {
   // The columns this map produces that a filter may NOT be pushed past, keyed by name. A
   // produced column blocks pushdown unless its assignment just reproduces the same-named column,
   // which the scan can still evaluate by name. A filter referencing a blocking
   // column reads a value that does not exist physically below the map, so pushing it down would
   // turn a valid plan into an invalid one (see #1371).
   //
   // Matching is by NAME: the produced column and a referencing filter carry identical
   // {name, type} identifiers (e.g. a decompression map's {segment1, STRING} output and a
   // hasMutation predicate resolved against it), so only the assignment's expression can
   // distinguish an unchanged column from a genuinely derived value.
   //
   // TODO(#1433): instead of blocking, fold the producing map's expression into the filter
   std::unordered_set<std::string> blocking_columns;
   for (const auto& assignment : node.assignments) {
      if (!isFieldRef(assignment)) {
         blocking_columns.insert(assignment.output_column.name);
      }
   }

   // Partition the accumulated filters: those referencing a blocking column must stay above
   // this map; the rest can continue to be pushed further down into the child.
   std::vector<std::unique_ptr<scalar_expressions::ScalarExpression>> filters_staying_above;
   std::vector<std::unique_ptr<scalar_expressions::ScalarExpression>> filters_to_push_down;
   for (auto& filter : current_filters) {
      const auto references_blocking_column =
         std::ranges::any_of(filter->freeIUs(), [&](const auto& column) {
            return blocking_columns.contains(column.name);
         });
      if (references_blocking_column) {
         filters_staying_above.push_back(std::move(filter));
      } else {
         filters_to_push_down.push_back(std::move(filter));
      }
   }

   current_filters = std::move(filters_to_push_down);
   propagateToNode(node.child);

   // Leave the blocking filters in `current_filters`; propagateToNode retains them above this map.
   current_filters = std::move(filters_staying_above);
   return nullptr;
}

operators::QueryNodePtr FilterPushdownPass::operator()(operators::TableScanNode& node) {
   current_filters.push_back(std::move(node.filter));
   node.filter = std::make_unique<And>(std::move(current_filters));
   current_filters.clear();
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
void FilterPushdownPass::barrier(operators::QueryNodePtr& child) {
   // A fresh pass keeps the child subtree's own filters from mixing with the filters above the
   // barrier: the latter stay in this pass's `current_filters` (retained above the barrier by
   // propagateToNode), while filters inside the child are pushed down (e.g. into the table scan, as
   // NodeResolutionPass requires beneath mutations()/insertions()).
   FilterPushdownPass child_pass;
   child_pass.propagateToNode(child);
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr FilterPushdownPass::operator()(operators::JoinNode& node) {
   // A filter sitting above a join is not turned into a pre-join filter. Which input a
   // predicate belongs to could be derived from freeIUs(), but pushing is only
   // semantics-preserving for some combinations: pushing into the null-supplying side of an
   // outer join changes the result (null-extended rows would no longer be filtered out), as
   // does pushing a predicate that references no column at all. Rather than push unsafely, any
   // filters above the join are left in `current_filters` and retained above the join by
   // propagateToNode (realized as an Arrow filter over the join output).
   //
   // The child subtrees may still contain FilterNodes of their own (e.g.
   // `join(data.filter(...), ...)`); each input is a barrier of its own so no state leaks
   // between the two branches or with the filters left above the join.
   barrier(node.left);
   barrier(node.right);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr FilterPushdownPass::operator()(operators::UnionAllNode& node) {
   // Push parent filters into both children. Clone for right, move originals into left.
   FilterPushdownPass right_pass;
   for (const auto& filter : current_filters) {
      right_pass.current_filters.push_back(filter->clone());
   }
   FilterPushdownPass left_pass;
   left_pass.current_filters = std::move(current_filters);
   current_filters.clear();

   left_pass.propagateToNode(node.left);
   right_pass.propagateToNode(node.right);
   return nullptr;
}

}  // namespace rhydb::query_engine::optimizer
