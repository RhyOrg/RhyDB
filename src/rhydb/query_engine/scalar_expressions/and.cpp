#include "rhydb/query_engine/scalar_expressions/and.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/intersection.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

using filter::operators::Intersection;
using filter::operators::Operator;
using filter::operators::OperatorVector;

And::And(ScalarExpressionVector&& children)
    : children(std::move(children)) {}

std::string And::toString() const {
   std::string res = "And(";
   res += joinWithLimit(children, " & ");
   res += ")";
   return res;
}

std::vector<schema::ColumnIdentifier> And::freeIUs() const {
   std::vector<schema::ColumnIdentifier> result;
   for (const auto& child : children) {
      for (auto& column : child->freeIUs()) {
         result.push_back(std::move(column));
      }
   }
   return result;
}

arrow::Result<arrow::compute::Expression> And::toArrowExpression() const {
   std::vector<arrow::compute::Expression> child_expressions;
   child_expressions.reserve(children.size());
   for (const auto& child : children) {
      ARROW_ASSIGN_OR_RAISE(auto child_expression, child->toArrowExpression());
      child_expressions.push_back(std::move(child_expression));
   }
   return arrow::compute::and_(child_expressions);
}

namespace {

void logCompiledChildren(
   OperatorVector& non_negated_child_operators,
   OperatorVector& negated_child_operators,
   filter::operators::PredicateVector& predicates
) {
   std::vector<std::string> child_operator_strings;
   std::ranges::transform(
      non_negated_child_operators,
      std::back_inserter(child_operator_strings),
      [&](const std::unique_ptr<filter::operators::Operator>& operator_) {
         return operator_->toString();
      }
   );
   std::ranges::transform(
      negated_child_operators,
      std::back_inserter(child_operator_strings),
      [&](const std::unique_ptr<filter::operators::Operator>& operator_) {
         return "!" + operator_->toString();
      }
   );
   std::vector<std::string> predicate_strings;
   std::ranges::transform(
      predicates,
      std::back_inserter(predicate_strings),
      [&](const std::unique_ptr<filter::operators::Predicate>& predicate) {
         return predicate->toString();
      }
   );
   SPDLOG_TRACE(
      "Compiled and processed child operators: {}, predicates {}, children: {}, negated children: "
      "{}, predicates: {}",
      fmt::join(child_operator_strings, ","),
      fmt::join(predicate_strings, ","),
      non_negated_child_operators.size(),
      negated_child_operators.size(),
      predicates.size()
   );
}
}  // namespace

std::tuple<OperatorVector, OperatorVector, filter::operators::PredicateVector> And::compileChildren(
   const storage::Table& table
) const {
   OperatorVector unprocessed_child_operators;
   std::ranges::transform(
      children,
      std::back_inserter(unprocessed_child_operators),
      [&](const std::unique_ptr<ScalarExpression>& expression) {
         return expression->compile(table);
      }
   );
   OperatorVector non_negated_child_operators;
   OperatorVector negated_child_operators;
   filter::operators::PredicateVector predicates;
   while (!unprocessed_child_operators.empty()) {
      auto child = std::move(unprocessed_child_operators.back());
      unprocessed_child_operators.pop_back();
      if (child->type() == filter::operators::FULL) {
         SPDLOG_TRACE("Skipping full child");
         continue;
      }
      if (child->type() == filter::operators::EMPTY) {
         SPDLOG_TRACE("Shortcutting because found empty child");
         OperatorVector empty;
         empty.emplace_back(std::make_unique<filter::operators::Empty>(table.row_layout));
         return {std::move(empty), OperatorVector(), filter::operators::PredicateVector{}};
      }
      if (child->type() == filter::operators::INTERSECTION) {
         auto* intersection_child = dynamic_cast<Intersection*>(child.get());
         appendVectorToVector(intersection_child->children, non_negated_child_operators);
         appendVectorToVector(intersection_child->negated_children, negated_child_operators);
      } else if (child->type() == filter::operators::COMPLEMENT) {
         negated_child_operators.emplace_back(filter::operators::Operator::negate(std::move(child))
         );
      } else if (child->type() == filter::operators::SELECTION) {
         auto* selection_child = dynamic_cast<filter::operators::Selection*>(child.get());
         appendVectorToVector<filter::operators::Predicate>(
            selection_child->predicates, predicates
         );
         SPDLOG_TRACE(
            "Found selection, appended {} predicates", selection_child->predicates.size()
         );
         if (selection_child->child_operator.has_value()) {
            auto child_operator = std::move(selection_child->child_operator.value());
            SPDLOG_TRACE("Appending child of selection {}", child_operator->toString());
            unprocessed_child_operators.emplace_back(std::move(child_operator));
         }
      } else {
         non_negated_child_operators.push_back(std::move(child));
      }
   }

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
   logCompiledChildren(non_negated_child_operators, negated_child_operators, predicates);
#endif

   return {
      std::move(non_negated_child_operators),
      std::move(negated_child_operators),
      std::move(predicates),
   };
}

std::unique_ptr<ScalarExpression> And::rewrite(const storage::Table& table, AmbiguityMode mode)
   const {
   ScalarExpressionVector rewritten_children;
   rewritten_children.reserve(children.size());
   for (const auto& child : children) {
      rewritten_children.emplace_back(child->rewrite(table, mode));
   }
   return std::make_unique<And>(std::move(rewritten_children));
}

std::unique_ptr<Operator> And::compile(const storage::Table& table) const {
   auto [non_negated_child_operators, negated_child_operators, predicates] = compileChildren(table);

   if (non_negated_child_operators.empty() && negated_child_operators.empty()) {
      if (predicates.empty()) {
         SPDLOG_TRACE(
            "Compiled And filter expression to Full, since no predicates and no child operators"
         );
         return std::make_unique<filter::operators::Full>(table.row_layout);
      }
      auto result =
         std::make_unique<filter::operators::Selection>(std::move(predicates), table.row_layout);
      SPDLOG_TRACE(
         "Compiled And filter expression to {} - found only predicates", result->toString()
      );

      return result;
   }

   std::unique_ptr<Operator> index_arithmetic_operator;
   if (non_negated_child_operators.size() == 1 && negated_child_operators.empty()) {
      index_arithmetic_operator = std::move(non_negated_child_operators[0]);
   } else if (non_negated_child_operators.empty()) {
      index_arithmetic_operator =
         Intersection::ofComplements(std::move(negated_child_operators), table.row_layout);
   } else {
      index_arithmetic_operator = std::make_unique<Intersection>(
         std::move(non_negated_child_operators),
         std::move(negated_child_operators),
         table.row_layout
      );
   }
   if (predicates.empty()) {
      SPDLOG_TRACE(
         "Compiled And filter expression to {} - found no predicates",
         index_arithmetic_operator->toString()
      );

      return index_arithmetic_operator;
   }
   auto result = std::make_unique<filter::operators::Selection>(
      std::move(index_arithmetic_operator), std::move(predicates), table.row_layout
   );

   SPDLOG_TRACE("Compiled And filter expression to {}", result->toString());

   return result;
}

}  // namespace rhydb::query_engine::scalar_expressions
