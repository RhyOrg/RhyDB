#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

class NOf : public ScalarExpression {
  private:
   ScalarExpressionVector children;
   int number_of_matchers;
   bool match_exactly;

   [[nodiscard]] ScalarExpressionVector rewriteChildren(
      const storage::Table& table,
      ScalarExpression::AmbiguityMode mode
   ) const;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewriteToNonExact(
      const storage::Table& table,
      ScalarExpression::AmbiguityMode mode
   ) const;

   [[nodiscard]] std::
      tuple<filter::operators::OperatorVector, filter::operators::OperatorVector, int>
      mapChildExpressions(const storage::Table& table) const;

  public:
   explicit NOf(ScalarExpressionVector&& children, int number_of_matchers, bool match_exactly);

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      ScalarExpressionVector cloned;
      cloned.reserve(children.size());
      for (const auto& child : children) {
         cloned.push_back(child->clone());
      }
      return std::make_unique<NOf>(std::move(cloned), number_of_matchers, match_exactly);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::N_OF;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
