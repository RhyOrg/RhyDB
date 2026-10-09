#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

class FloatBetween : public ScalarExpression {
  private:
   schema::ColumnIdentifier column;
   std::optional<double> from;
   std::optional<double> to;

  public:
   explicit FloatBetween(
      schema::ColumnIdentifier column,
      std::optional<double> from,
      std::optional<double> to
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<FloatBetween>(column, from, to);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::FLOAT_BETWEEN;
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
