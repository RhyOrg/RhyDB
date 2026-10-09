#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

class And : public ScalarExpression {
  private:
   ScalarExpressionVector children;

   [[nodiscard]] std::tuple<
      filter::operators::OperatorVector,
      filter::operators::OperatorVector,
      filter::operators::PredicateVector>
   compileChildren(const storage::Table& table) const;

  public:
   explicit And(ScalarExpressionVector&& children);

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      ScalarExpressionVector cloned;
      cloned.reserve(children.size());
      for (const auto& child : children) {
         cloned.push_back(child->clone());
      }
      return std::make_unique<And>(std::move(cloned));
   }

   /// Moves the conjuncts out of this And, leaving it empty. Used by the optimizer to split a
   /// top-level conjunction so each conjunct can be pushed independently.
   [[nodiscard]] ScalarExpressionVector takeChildren() { return std::move(children); }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::AND;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
