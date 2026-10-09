#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

class Or : public ScalarExpression {
   ScalarExpressionVector children;

  public:
   explicit Or(ScalarExpressionVector&& children);

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      ScalarExpressionVector cloned;
      cloned.reserve(children.size());
      for (const auto& child : children) {
         cloned.push_back(child->clone());
      }
      return std::make_unique<Or>(std::move(cloned));
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::OR;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] static std::vector<const ScalarExpression*> collectChildren(
      const ScalarExpressionVector& children
   );

   [[nodiscard]] static ScalarExpressionVector algebraicSimplification(
      ScalarExpressionVector unprocessed_child_expressions
   );

   template <typename SymbolType>
   [[nodiscard]] static ScalarExpressionVector rewriteSymbolInSetExpressions(
      ScalarExpressionVector children
   );

   [[nodiscard]] static ScalarExpressionVector mergeStringInSetExpressions(
      ScalarExpressionVector children
   );

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
