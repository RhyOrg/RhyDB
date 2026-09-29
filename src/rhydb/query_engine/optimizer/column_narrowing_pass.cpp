#include "rhydb/query_engine/optimizer/column_narrowing_pass.h"

#include <algorithm>
#include <vector>

#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/join_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/schema_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/transitive_closure_node.h"
#include "rhydb/query_engine/operators/union_all_node.h"

namespace rhydb::query_engine::optimizer {

ColumnNarrowingPass ColumnNarrowingPass::makePass(const operators::QueryNodePtr& node) {
   return ColumnNarrowingPass{node->getOutputSchema()};
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::FilterNode& node) {
   // A filter needs every column its predicate references from its child, in addition to the
   // columns the parent requires. Keeping them required prevents a node below the filter that
   // produces such a column (e.g. a map) from being pruned away while the filter still needs it.
   for (const auto& column : node.filter->freeIUs()) {
      if (std::ranges::find(required, column) == required.end()) {
         required.push_back(column);
      }
   }
   propagateToNode(node.child);
   return nullptr;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static,misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::TableScanNode& node) {
   std::vector<schema::ColumnIdentifier> pruned;
   for (const auto& col : node.fields) {
      if (std::ranges::find(required, col) != required.end()) {
         pruned.push_back(col);
      }
   }
   node.fields = std::move(pruned);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::AggregateNode& node) {
   std::vector<schema::ColumnIdentifier> child_required;
   child_required.reserve(node.group_by_fields.size());
   for (const auto& field : node.group_by_fields) {
      child_required.push_back(field);
   }
   for (const auto& agg : node.aggregates) {
      if (agg.source_column.has_value()) {
         child_required.push_back(agg.source_column.value());
      }
   }

   if (child_required.empty()) {
      // COUNT(*) with no group-by: still need one column to drive the row stream.
      auto child_schema = node.child->getOutputSchema();
      RHYDB_ASSERT(!child_schema.empty());
      required = RequiredColumns{child_schema.front()};
   } else {
      required = std::move(child_required);
   }
   propagateToNode(node.child);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::ProjectNode& node) {
   std::vector<schema::ColumnIdentifier> narrowed_fields;
   for (const auto& field : node.fields) {
      if (std::ranges::find(required, field) != required.end()) {
         narrowed_fields.push_back(field);
      }
   }
   node.fields = narrowed_fields;

   required = node.fields;
   propagateToNode(node.child);
   if (node.child->getOutputSchema() == node.fields) {
      return std::move(node.child);
   }
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::MapNode& node) {
   // A MapNode keeps all of its child's columns and adds (or replaces) some via
   // scalar assignments. We only keep assignments that produce required output columns;
   // the child must provide the referenced columns for those assignments plus any
   // required pass-through columns.
   //
   // Output columns are identified by name (MapNode::getOutputSchema and addToExecPlan
   // both key assignments by name) and handleMap guarantees the output names are unique,
   // so each required column matches at most one assignment by name. The child likewise
   // cannot expose two columns of the same name, so child requirements are deduplicated by
   // name rather than by full ColumnIdentifier — a decompression assignment requests its
   // input column as a sequence type while a pass-through of the same name would request
   // it as STRING.
   RequiredColumns child_required;

   const auto already_required = [&](const schema::ColumnIdentifier& column) {
      return std::ranges::any_of(child_required, [&](const auto& existing) {
         return existing.name == column.name;
      });
   };

   const auto is_required_output = [&](const operators::MapNode::Assignment& assignment) {
      return std::ranges::any_of(required, [&](const auto& required_column) {
         return required_column.name == assignment.output_column.name;
      });
   };

   // Walk the assignments once in their original order, keeping those that produce a
   // required output column. Preserving the original order matters: getOutputSchema
   // appends newly-added columns in assignment order, so reordering would change the
   // projection's output column order.
   std::vector<operators::MapNode::Assignment> kept_assignments;
   for (auto& assignment : node.assignments) {
      if (!is_required_output(assignment)) {
         continue;
      }
      for (const auto& referenced_column : assignment.expression->freeIUs()) {
         if (!already_required(referenced_column)) {
            child_required.push_back(referenced_column);
         }
      }
      kept_assignments.push_back(std::move(assignment));
   }

   // Required columns not produced by any assignment are pass-through columns that the
   // child must provide directly.
   for (const auto& required_column : required) {
      const auto produced_by_assignment =
         std::ranges::any_of(kept_assignments, [&](const auto& assignment) {
            return assignment.output_column.name == required_column.name;
         });
      if (!produced_by_assignment && !already_required(required_column)) {
         child_required.push_back(required_column);
      }
   }

   node.assignments = std::move(kept_assignments);

   if (child_required.empty()) {
      // Even when all output columns are produced by assignments, the child must
      // emit at least one field so that row identity is preserved.
      auto child_schema = node.child->getOutputSchema();
      RHYDB_ASSERT(!child_schema.empty());
      child_required.push_back(child_schema.front());
   }
   required = std::move(child_required);
   propagateToNode(node.child);

   if (node.assignments.empty()) {
      return std::move(node.child);
   }
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::OrderByNode& node) {
   for (const auto& field : node.fields) {
      if (std::ranges::find(required, field.field) == required.end()) {
         required.push_back(field.field);
      }
   }
   propagateToNode(node.child);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::SchemaNode& node) {
   // schema() reports its child's full output schema, so NO column may be pruned from the
   // child (unlike the base default, which would prune against this pass's `required` set —
   // seeded from the root, i.e. schema()'s own {fieldName, type} columns — and corrupt the
   // reported schema). We still recurse with a fresh pass seeded with the child's complete
   // output schema as "required": this keeps every column while still allowing the pass's
   // structural simplifications to run.
   ColumnNarrowingPass child_pass{node.child->getOutputSchema()};
   child_pass.propagateToNode(node.child);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion,readability-make-member-function-const)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::TransitiveClosureNode& node) {
   auto child_required = node.child->getOutputSchema();
   std::erase_if(child_required, [&node](const auto& column) {
      return column.name != node.from_column && column.name != node.to_column;
   });
   ColumnNarrowingPass child_pass{std::move(child_required)};
   child_pass.propagateToNode(node.child);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion,readability-make-member-function-const)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::JoinNode& node) {
   // Each input must provide its join keys plus those of its columns that are required from the
   // join output. The side dropped by a semi/anti join contributes no output columns, so only its
   // keys are needed. Both inputs are narrowed with a pass of their own, as `required` differs.
   using arrow::acero::JoinType;
   const bool left_output_dropped =
      node.join_type == JoinType::RIGHT_SEMI || node.join_type == JoinType::RIGHT_ANTI;
   const bool right_output_dropped =
      node.join_type == JoinType::LEFT_SEMI || node.join_type == JoinType::LEFT_ANTI;
   const auto input_required = [&](
                                  const operators::QueryNode& input,
                                  const std::vector<schema::ColumnIdentifier>& keys,
                                  bool output_dropped
                               ) {
      RequiredColumns columns{keys.begin(), keys.end()};
      if (output_dropped) {
         return columns;
      }
      for (const auto& column : input.getOutputSchema()) {
         if (std::ranges::find(required, column) != required.end() &&
             std::ranges::find(columns, column) == columns.end()) {
            columns.push_back(column);
         }
      }
      return columns;
   };
   ColumnNarrowingPass left_pass{input_required(*node.left, node.left_keys, left_output_dropped)};
   ColumnNarrowingPass right_pass{input_required(*node.right, node.right_keys, right_output_dropped)
   };
   left_pass.propagateToNode(node.left);
   right_pass.propagateToNode(node.right);
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion,readability-make-member-function-const)
operators::QueryNodePtr ColumnNarrowingPass::operator()(operators::UnionAllNode& node) {
   // Narrow columns in each child independently using the same required set. Each branch needs
   // its own pass instance because `required` is mutated while walking, so the base default
   // (which shares a single instance across both branches) is not correct here.
   ColumnNarrowingPass left_pass(required);
   left_pass.propagateToNode(node.left);
   ColumnNarrowingPass right_pass(required);
   right_pass.propagateToNode(node.right);
   return nullptr;
}

}  // namespace rhydb::query_engine::optimizer
