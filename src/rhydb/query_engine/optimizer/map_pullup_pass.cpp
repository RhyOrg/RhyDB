#include "rhydb/query_engine/optimizer/map_pullup_pass.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/iso_week.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"

namespace rhydb::query_engine::optimizer {

namespace {

using scalar_expressions::ScalarExpression;

/// Rebuilds `expression` with every `FieldRef` to a column in `produced` replaced by a clone of
/// that column's defining expression, inlining a lower map's assignments into an upper map's
/// expression.
///
/// Returns nullptr when the expression cannot be safely inlined — it references a produced column
/// through a construct this function does not know how to rewrite (e.g. an `Equals` or another
/// predicate that stores bare column identifiers) — in which case the caller must decline the
/// merge. A leaf constant, or any expression that references none of the produced columns, is
/// cloned unchanged; recognising "references no produced column" via `freeIUs()` keeps this robust
/// to expression kinds (new literal types, predicates) it has no explicit case for.
std::unique_ptr<ScalarExpression> substituteColumns(
   const ScalarExpression& expression,
   const std::unordered_map<std::string, const ScalarExpression*>& produced
) {
   using Kind = ScalarExpression::Kind;
   switch (expression.kind()) {
      case Kind::FIELD_REF: {
         const auto& field_ref = static_cast<const scalar_expressions::FieldRef&>(expression);
         const auto replacement = produced.find(field_ref.column.name);
         return replacement != produced.end() ? replacement->second->clone() : expression.clone();
      }
      case Kind::AT: {
         const auto& at = static_cast<const scalar_expressions::At&>(expression);
         auto input = substituteColumns(*at.input, produced);
         return input == nullptr
                   ? nullptr
                   : std::make_unique<scalar_expressions::At>(std::move(input), at.position);
      }
      case Kind::ISO_WEEK: {
         const auto& iso_week = static_cast<const scalar_expressions::IsoWeek&>(expression);
         auto input = substituteColumns(*iso_week.input, produced);
         return input == nullptr ? nullptr
                                 : std::make_unique<scalar_expressions::IsoWeek>(std::move(input));
      }
      case Kind::ZSTD_DECOMPRESS_SCALAR: {
         const auto& zstd =
            static_cast<const scalar_expressions::ZstdDecompressScalar&>(expression);
         auto input = substituteColumns(*zstd.input, produced);
         return input == nullptr ? nullptr
                                 : std::make_unique<scalar_expressions::ZstdDecompressScalar>(
                                      std::move(input), zstd.dictionary_string
                                   );
      }
      default:
         // Any other expression: we cannot rewrite its column references, so keeping it is only
         // safe when it references none of the produced columns. Otherwise decline the merge.
         for (const auto& free_iu : expression.freeIUs()) {
            if (produced.contains(free_iu.name)) {
               return nullptr;
            }
         }
         return expression.clone();
   }
}

}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr MapPullupPass::operator()(operators::FetchNode& node) {
   propagateToNode(node.child);

   // A FetchNode only limits/offsets rows; it references no columns, so a MapNode below
   // it can always be pulled up.
   if (node.child->kind() != operators::NodeKind::MAP) {
      return nullptr;
   }
   operators::QueryNodePtr map_owner = std::move(node.child);
   auto& map = static_cast<operators::MapNode&>(*map_owner);

   auto new_fetch =
      std::make_unique<operators::FetchNode>(std::move(map.child), node.count, node.offset);
   map.child = std::move(new_fetch);
   return map_owner;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr MapPullupPass::operator()(operators::OrderByNode& node) {
   propagateToNode(node.child);

   if (node.child->kind() != operators::NodeKind::MAP) {
      return nullptr;
   }
   auto& map = static_cast<operators::MapNode&>(*node.child);

   // The columns the MapNode produces (newly added or replaced in place).
   std::unordered_set<std::string> produced_columns;
   for (const auto& assignment : map.assignments) {
      produced_columns.insert(assignment.output_column.name);
   }

   // Pulling the MapNode above the OrderBy is only safe when no order-by field references a
   // produced column. Otherwise the OrderBy below would sort on a different value (a replaced
   // column) or a column that does not exist yet (a newly added one), changing the result.
   for (const auto& field : node.fields) {
      if (produced_columns.contains(field.field.name)) {
         return nullptr;
      }
   }

   operators::QueryNodePtr map_owner = std::move(node.child);
   auto new_order_by = std::make_unique<operators::OrderByNode>(
      std::move(map.child), std::move(node.fields), node.randomize_seed
   );
   map.child = std::move(new_order_by);
   return map_owner;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr MapPullupPass::operator()(operators::MapNode& node) {
   // Merge child-first, so a chain of maps collapses bottom-up into `node`'s child before we merge.
   propagateToNode(node.child);

   if (node.child->kind() != operators::NodeKind::MAP) {
      return nullptr;
   }
   auto& lower = static_cast<operators::MapNode&>(*node.child);

   // The columns the lower map produces, mapped to their defining expressions: an upper expression
   // referencing one of these reads the lower map's computed value, so it must be inlined.
   std::unordered_map<std::string, const ScalarExpression*>
      lower_produced_expressions_by_output_name;
   for (const auto& assignment : lower.assignments) {
      lower_produced_expressions_by_output_name.emplace(
         assignment.output_column.name, assignment.expression.get()
      );
   }

   // Inline lower-produced columns into the upper expressions up front. If any expression cannot be
   // safely rewritten, decline the merge and leave both maps untouched (nothing has been moved
   // yet).
   std::vector<std::unique_ptr<ScalarExpression>> substituted_expressions;
   substituted_expressions.reserve(node.assignments.size());
   for (const auto& assignment : node.assignments) {
      auto expression =
         substituteColumns(*assignment.expression, lower_produced_expressions_by_output_name);
      if (expression == nullptr) {
         return nullptr;
      }
      substituted_expressions.push_back(std::move(expression));
   }

   std::unordered_set<std::string> upper_outputs;
   for (const auto& assignment : node.assignments) {
      upper_outputs.insert(assignment.output_column.name);
   }

   std::vector<operators::MapNode::Assignment> merged;
   merged.reserve(lower.assignments.size() + node.assignments.size());
   // Keep the lower map's assignments the upper map does not overwrite: they are lower-produced
   // columns the upper map passes through, and they read the (now shared) grandchild directly.
   for (auto& assignment : lower.assignments) {
      if (!upper_outputs.contains(assignment.output_column.name)) {
         merged.push_back(std::move(assignment));
      }
   }
   // The upper map's assignments, with references to lower-produced columns now inlined.
   for (size_t i = 0; i < node.assignments.size(); ++i) {
      merged.push_back(
         {.output_column = node.assignments[i].output_column,
          .expression = std::move(substituted_expressions[i]),}
      );
   }

   return std::make_unique<operators::MapNode>(std::move(lower.child), std::move(merged));
}

}  // namespace rhydb::query_engine::optimizer
