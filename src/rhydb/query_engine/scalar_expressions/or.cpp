#include "rhydb/query_engine/scalar_expressions/or.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>

#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/union.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/string_in_set.h"
#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"

namespace rhydb::query_engine::scalar_expressions {

using filter::operators::OperatorVector;

Or::Or(ScalarExpressionVector&& children)
    : children(std::move(children)) {}

std::string Or::toString() const {
   std::string res = "Or(";
   res += joinWithLimit(children, " | ");
   res += ")";
   return res;
}

std::vector<schema::ColumnIdentifier> Or::freeIUs() const {
   std::vector<schema::ColumnIdentifier> result;
   for (const auto& child : children) {
      for (auto& column : child->freeIUs()) {
         result.push_back(std::move(column));
      }
   }
   return result;
}

arrow::Result<arrow::compute::Expression> Or::toArrowExpression() const {
   std::vector<arrow::compute::Expression> child_expressions;
   child_expressions.reserve(children.size());
   for (const auto& child : children) {
      ARROW_ASSIGN_OR_RAISE(auto child_expression, child->toArrowExpression());
      child_expressions.push_back(std::move(child_expression));
   }
   return arrow::compute::or_(child_expressions);
}

std::vector<const ScalarExpression*> Or::collectChildren(const ScalarExpressionVector& children) {
   std::vector<const ScalarExpression*> result;

   std::vector<const ScalarExpression*> queue;
   for (const auto& direct_child : children) {
      queue.push_back(direct_child.get());
   }

   while (!queue.empty()) {
      const auto* current = queue.back();
      queue.pop_back();
      if (const auto* or_child = dynCast<Or>(current)) {
         for (const auto& child : or_child->children) {
            queue.push_back(child.get());
         }
      } else {
         result.push_back(current);
      }
   }
   return result;
}

ScalarExpressionVector Or::algebraicSimplification(
   ScalarExpressionVector unprocessed_child_expressions
) {
   ScalarExpressionVector non_trivial_children;
   while (!unprocessed_child_expressions.empty()) {
      auto child = std::move(unprocessed_child_expressions.back());
      unprocessed_child_expressions.pop_back();
      if (const auto* bool_literal = dynCast<BoolLiteral>(child.get())) {
         if (bool_literal->value) {
            SPDLOG_TRACE("Shortcutting because found constant-true child");
            ScalarExpressionVector singleton_true;
            singleton_true.emplace_back(std::make_unique<BoolLiteral>(true));
            return singleton_true;
         }
         SPDLOG_TRACE("Skipping constant-false child");
         continue;
      }
      if (auto* or_child = dynCast<Or>(child.get())) {
         appendVectorToVector(or_child->children, unprocessed_child_expressions);
      } else {
         non_trivial_children.push_back(std::move(child));
      }
   }
   return non_trivial_children;
}

template <typename SymbolType>
ScalarExpressionVector Or::rewriteSymbolInSetExpressions(ScalarExpressionVector children) {
   ScalarExpressionVector new_children;
   using ColumnAndPosition = std::pair<schema::ColumnIdentifier, uint32_t>;
   using Symbols = std::vector<typename SymbolType::Symbol>;
   std::map<ColumnAndPosition, Symbols> symbol_in_set_children;
   for (auto& child : children) {
      if (auto* symbol_in_set_child = dynCast<SymbolInSet<SymbolType>>(child.get())) {
         std::vector<typename SymbolType::Symbol>& symbols_so_far = symbol_in_set_children[{
            symbol_in_set_child->column, symbol_in_set_child->position_idx,
         }];
         std::ranges::copy(symbol_in_set_child->symbols, std::back_inserter(symbols_so_far));
      } else {
         new_children.emplace_back(std::move(child));
      }
   }

   for (auto& [column_and_position, symbols] : symbol_in_set_children) {
      new_children.emplace_back(std::make_unique<SymbolInSet<SymbolType>>(
         column_and_position.first, column_and_position.second, std::move(symbols)
      ));
   }

   return new_children;
}

template ScalarExpressionVector Or::rewriteSymbolInSetExpressions<AminoAcid>(
   ScalarExpressionVector children
);
template ScalarExpressionVector Or::rewriteSymbolInSetExpressions<Nucleotide>(
   ScalarExpressionVector children
);

namespace {
void appendStringSetToStringSet(
   std::unordered_set<std::string> from,
   std::unordered_set<std::string>& target
) {
   for (auto iter = from.begin(); iter != from.end();) {
      auto current = iter++;
      auto node = from.extract(current);
      target.insert(std::move(node.value()));
   }
}
}  // namespace

ScalarExpressionVector Or::mergeStringInSetExpressions(ScalarExpressionVector children) {
   ScalarExpressionVector new_children;
   using Column = schema::ColumnIdentifier;
   using Strings = std::unordered_set<std::string>;
   std::map<Column, Strings> new_string_in_set_children;
   for (auto& child : children) {
      if (auto* string_in_set_child = dynCast<StringInSet>(child.get())) {
         if (auto iter = new_string_in_set_children.find(string_in_set_child->column);
             iter != new_string_in_set_children.end()) {
            auto& new_string_in_set_child_for_column = iter->second;
            appendStringSetToStringSet(
               std::move(string_in_set_child->values), new_string_in_set_child_for_column
            );
         } else {
            new_string_in_set_children.emplace(
               std::move(string_in_set_child->column), std::move(string_in_set_child->values)
            );
         }
      } else {
         new_children.emplace_back(std::move(child));
      }
   }

   for (auto iter = new_string_in_set_children.begin(); iter != new_string_in_set_children.end();) {
      auto current = iter++;
      auto node = new_string_in_set_children.extract(current);

      new_children.emplace_back(
         std::make_unique<StringInSet>(std::move(node.key()), std::move(node.mapped()))
      );
   }

   return new_children;
}

std::unique_ptr<ScalarExpression> Or::rewrite(
   const storage::Table& table,
   ScalarExpression::AmbiguityMode mode
) const {
   std::vector<const ScalarExpression*> collected_children = collectChildren(children);
   ScalarExpressionVector rewritten_children;
   std::ranges::transform(
      collected_children,
      std::back_inserter(rewritten_children),
      [&](const ScalarExpression* child) { return child->rewrite(table, mode); }
   );
   rewritten_children = algebraicSimplification(std::move(rewritten_children));
   rewritten_children = rewriteSymbolInSetExpressions<Nucleotide>(std::move(rewritten_children));
   rewritten_children = rewriteSymbolInSetExpressions<AminoAcid>(std::move(rewritten_children));
   rewritten_children = mergeStringInSetExpressions(std::move(rewritten_children));
   if (rewritten_children.size() == 1) {
      return std::move(rewritten_children[0]);
   }
   return std::make_unique<Or>(std::move(rewritten_children));
}

std::unique_ptr<filter::operators::Operator> Or::compile(const storage::Table& table) const {
   OperatorVector all_child_operators;
   std::ranges::transform(
      children,
      std::back_inserter(all_child_operators),
      [&](const std::unique_ptr<ScalarExpression>& expression) {
         return expression->compile(table);
      }
   );
   OperatorVector filtered_child_operators;
   for (auto& child : all_child_operators) {
      if (child->type() == filter::operators::EMPTY) {
         continue;
      }
      if (child->type() == filter::operators::FULL) {
         return std::make_unique<filter::operators::Full>(table.row_layout);
      }
      if (child->type() == filter::operators::UNION) {
         auto* or_child = dynamic_cast<filter::operators::Union*>(child.get());
         std::ranges::transform(
            or_child->children,
            std::back_inserter(filtered_child_operators),
            [&](std::unique_ptr<filter::operators::Operator>& expression) {
               return std::move(expression);
            }
         );
      } else {
         filtered_child_operators.push_back(std::move(child));
      }
   }
   if (filtered_child_operators.empty()) {
      return std::make_unique<filter::operators::Empty>(table.row_layout);
   }
   if (filtered_child_operators.size() == 1) {
      return std::move(filtered_child_operators[0]);
   }

   if (std::ranges::any_of(filtered_child_operators, [](const auto& child) {
          return child->type() == filter::operators::COMPLEMENT;
       })) {
      return filter::operators::Complement::fromDeMorgan(
         std::move(filtered_child_operators), table.row_layout
      );
   }
   return std::make_unique<filter::operators::Union>(
      std::move(filtered_child_operators), table.row_layout
   );
}

}  // namespace rhydb::query_engine::scalar_expressions
