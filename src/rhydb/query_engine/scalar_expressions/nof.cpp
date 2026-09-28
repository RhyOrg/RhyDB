#include "rhydb/query_engine/scalar_expressions/nof.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/intersection.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/threshold.h"
#include "rhydb/query_engine/filter/operators/union.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/and.h"
#include "rhydb/query_engine/scalar_expressions/negation.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace {

using Operator = rhydb::query_engine::filter::operators::Operator;
using OperatorVector = rhydb::query_engine::filter::operators::OperatorVector;
using Empty = rhydb::query_engine::filter::operators::Empty;
using Full = rhydb::query_engine::filter::operators::Full;
using Complement = rhydb::query_engine::filter::operators::Complement;
using Intersection = rhydb::query_engine::filter::operators::Intersection;
using Union = rhydb::query_engine::filter::operators::Union;
using Threshold = rhydb::query_engine::filter::operators::Threshold;
using RowLayout = rhydb::storage::column::RowLayout;

std::unique_ptr<Operator> handleTrivialCases(
   const int updated_number_of_matchers,
   OperatorVector& non_negated_child_operators,
   OperatorVector& negated_child_operators,
   bool match_exactly,
   const RowLayout& row_layout
) {
   const int child_operator_count =
      static_cast<int>(non_negated_child_operators.size() + negated_child_operators.size());

   if (updated_number_of_matchers > child_operator_count) {
      return std::make_unique<Empty>(row_layout);
   }
   if (updated_number_of_matchers < 0) {
      if (match_exactly) {
         return std::make_unique<Empty>(row_layout);
      }
      return std::make_unique<Full>(row_layout);
   }
   if (updated_number_of_matchers == 0) {
      if (!match_exactly) {
         return std::make_unique<Full>(row_layout);
      }
      /// Now we want to match exactly none
      if (child_operator_count == 0) {
         return std::make_unique<Full>(row_layout);
      }
      if (child_operator_count == 1) {
         if (non_negated_child_operators.empty()) {
            return std::move(negated_child_operators[0]);
         }
         return std::make_unique<Complement>(std::move(non_negated_child_operators[0]), row_layout);
      }
      /// To negate entire result Not(Union) => Intersection(Not(Non-negated),Not(Negated))
      /// equiv: Intersection(Negated, Non-Negated) or Not(Union(Non-negated)), if negated empty
      if (negated_child_operators.empty()) {
         auto union_ret =
            std::make_unique<Union>(std::move(non_negated_child_operators), row_layout);
         return std::make_unique<Complement>(std::move(union_ret), row_layout);
      }
      return std::make_unique<Intersection>(
         std::move(negated_child_operators), std::move(non_negated_child_operators), row_layout
      );
   }
   if (updated_number_of_matchers == 1 && child_operator_count == 1) {
      if (negated_child_operators.empty()) {
         return std::move(non_negated_child_operators[0]);
      }
      return std::make_unique<Complement>(std::move(negated_child_operators[0]), row_layout);
   }
   return nullptr;
}

std::unique_ptr<Operator> handleAndCase(
   OperatorVector& non_negated_child_operators,
   OperatorVector& negated_child_operators,
   const RowLayout& row_layout
) {
   if (non_negated_child_operators.empty()) {
      std::unique_ptr<Union> union_ret =
         std::make_unique<Union>(std::move(negated_child_operators), row_layout);
      return std::make_unique<Complement>(std::move(union_ret), row_layout);
   }
   return std::make_unique<Intersection>(
      std::move(non_negated_child_operators), std::move(negated_child_operators), row_layout
   );
}

std::unique_ptr<Operator> handleOrCase(
   OperatorVector& non_negated_child_operators,
   OperatorVector& negated_child_operators,
   const RowLayout& row_layout
) {
   if (negated_child_operators.empty()) {
      return std::make_unique<Union>(std::move(non_negated_child_operators), row_layout);
   }
   /// De'Morgan if at least one negated
   std::unique_ptr<Intersection> intersection_ret = std::make_unique<Intersection>(
      std::move(negated_child_operators), std::move(non_negated_child_operators), row_layout
   );
   return std::make_unique<Complement>(std::move(intersection_ret), row_layout);
}

std::unique_ptr<Operator> toOperator(
   const int updated_number_of_matchers,
   OperatorVector&& non_negated_child_operators,
   OperatorVector&& negated_child_operators,
   bool match_exactly,
   const RowLayout& row_layout
) {
   auto tmp = handleTrivialCases(
      updated_number_of_matchers,
      non_negated_child_operators,
      negated_child_operators,
      match_exactly,
      row_layout
   );
   if (tmp) {
      return tmp;
   }

   const int child_operator_count =
      static_cast<int>(non_negated_child_operators.size() + negated_child_operators.size());

   if (updated_number_of_matchers == child_operator_count) {
      return handleAndCase(non_negated_child_operators, negated_child_operators, row_layout);
   }
   if (updated_number_of_matchers == 1 && !match_exactly) {
      return handleOrCase(non_negated_child_operators, negated_child_operators, row_layout);
   }
   return std::make_unique<Threshold>(
      std::move(non_negated_child_operators),
      std::move(negated_child_operators),
      updated_number_of_matchers,
      match_exactly,
      row_layout
   );
}

}  // namespace

namespace rhydb::query_engine::scalar_expressions {

NOf::NOf(ScalarExpressionVector&& children, int number_of_matchers, bool match_exactly)
    : children(std::move(children)),
      number_of_matchers(number_of_matchers),
      match_exactly(match_exactly) {}

std::string NOf::toString() const {
   std::string res;
   if (match_exactly) {
      res = "[exactly-" + std::to_string(number_of_matchers) + "-of:";
   } else {
      res = "[" + std::to_string(number_of_matchers) + "-of:";
   }
   res += joinWithLimit(children);
   res += "]";
   return res;
}

std::vector<schema::ColumnIdentifier> NOf::freeIUs() const {
   std::vector<schema::ColumnIdentifier> result;
   for (const auto& child : children) {
      for (auto& column : child->freeIUs()) {
         result.push_back(std::move(column));
      }
   }
   return result;
}

std::tuple<filter::operators::OperatorVector, filter::operators::OperatorVector, int> NOf::
   mapChildExpressions(const storage::Table& table) const {
   filter::operators::OperatorVector child_operators;
   child_operators.reserve(children.size());
   for (const auto& child_expression : children) {
      child_operators.push_back(child_expression->compile(table));
   }

   filter::operators::OperatorVector non_negated_child_operators;
   filter::operators::OperatorVector negated_child_operators;
   int updated_number_of_matchers = number_of_matchers;

   for (auto& child_operator : child_operators) {
      if (child_operator->type() == filter::operators::EMPTY) {
         continue;
      }
      if (child_operator->type() == filter::operators::FULL) {
         updated_number_of_matchers--;
         continue;
      }
      if (child_operator->type() == filter::operators::COMPLEMENT) {
         auto canceled_negation = filter::operators::Operator::negate(std::move(child_operator));
         negated_child_operators.emplace_back(std::move(canceled_negation));
         continue;
      }
      non_negated_child_operators.push_back(std::move(child_operator));
   }
   return std::tuple<filter::operators::OperatorVector, filter::operators::OperatorVector, int>{
      std::move(non_negated_child_operators),
      std::move(negated_child_operators),
      updated_number_of_matchers,
   };
}

ScalarExpressionVector NOf::rewriteChildren(
   const storage::Table& table,
   ScalarExpression::AmbiguityMode mode
) const {
   ScalarExpressionVector rewritten_children;
   rewritten_children.reserve(children.size());
   for (const auto& child : children) {
      rewritten_children.push_back(child->rewrite(table, mode));
   }
   return rewritten_children;
}

std::unique_ptr<ScalarExpression> NOf::rewriteToNonExact(
   const storage::Table& table,
   ScalarExpression::AmbiguityMode mode
) const {
   auto at_least_k = std::make_unique<NOf>(
      rewriteChildren(table, mode),
      this->number_of_matchers,
      /*match_exactly=*/false
   );
   auto at_least_k_plus_one = std::make_unique<NOf>(
      rewriteChildren(table, mode),
      this->number_of_matchers + 1,
      /*match_exactly=*/false
   );
   ScalarExpressionVector and_children;
   and_children.push_back(std::move(at_least_k));
   and_children.push_back(std::make_unique<Negation>(std::move(at_least_k_plus_one)));
   return std::make_unique<And>(std::move(and_children));
}

std::unique_ptr<ScalarExpression> NOf::rewrite(const storage::Table& table, AmbiguityMode mode)
   const {
   // We cannot easily map ambiguity modes through an exact NOf expression -> rewrite without exact
   if (mode != NONE && match_exactly && std::cmp_less(number_of_matchers, children.size())) {
      return rewriteToNonExact(table, mode);
   }

   return std::make_unique<NOf>(rewriteChildren(table, mode), number_of_matchers, match_exactly);
}

std::unique_ptr<filter::operators::Operator> NOf::compile(const storage::Table& table) const {
   auto [non_negated_child_operators, negated_child_operators, updated_number_of_matchers] =
      mapChildExpressions(table);

   if (updated_number_of_matchers < 0) {
      if (match_exactly) {
         return std::make_unique<filter::operators::Empty>(table.row_layout);
      }
      return std::make_unique<filter::operators::Full>(table.row_layout);
   }

   return toOperator(
      updated_number_of_matchers,
      std::move(non_negated_child_operators),
      std::move(negated_child_operators),
      match_exactly,
      table.row_layout
   );
}

}  // namespace rhydb::query_engine::scalar_expressions
