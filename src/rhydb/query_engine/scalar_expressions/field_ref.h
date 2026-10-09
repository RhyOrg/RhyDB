#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// References an existing column by name. As a scalar expression it evaluates to
/// that column's value per row, e.g. `y := age`; its type() is the referenced
/// column's type.
class FieldRef : public ScalarExpression {
  public:
   schema::ColumnIdentifier column;

   explicit FieldRef(schema::ColumnIdentifier column);

   [[nodiscard]] schema::ColumnType type() const override { return column.type; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<FieldRef>(column);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::FIELD_REF;
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
