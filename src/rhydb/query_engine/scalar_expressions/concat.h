#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// A scalar expression that evaluates to the concatenation of the two strings its child
/// expressions evaluate to. If either side is null, the result is null.
class Concat : public ScalarExpression {
  public:
   std::unique_ptr<ScalarExpression> left;
   std::unique_ptr<ScalarExpression> right;

   Concat(std::unique_ptr<ScalarExpression> left, std::unique_ptr<ScalarExpression> right);

   [[nodiscard]] schema::DataType type() const override { return schema::DataType::STRING; }

   static constexpr Kind KIND = Kind::CONCAT;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<Concat>(left->clone(), right->clone());
   }

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
