#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// Whether values of the (plain) type `from` can be cast to the (plain) type `to` with `Cast`.
/// A cast to the same type is supported, but is a no-op.
[[nodiscard]] bool isCastSupported(const schema::DataType& from, const schema::DataType& to);

/// A scalar expression that converts the value of its child expression to `target_type`, e.g.
/// `age::float`. A null input stays null. Values that cannot be converted (overflow, a fractional
/// part cast to an integer, an unparseable string) fail the execution.
class Cast : public ScalarExpression {
  public:
   std::unique_ptr<ScalarExpression> input;
   schema::DataType target_type;

   Cast(std::unique_ptr<ScalarExpression> input, schema::DataType target_type);

   [[nodiscard]] schema::DataType type() const override { return target_type; }

   static constexpr Kind KIND = Kind::CAST;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<Cast>(input->clone(), target_type);
   }

   /// A cast cannot be evaluated while scanning a table, only by Arrow (e.g. after a `map`).
   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

/// Evaluates `input` (an expression that reads no column, e.g. a literal) cast to `target_type`
/// once and returns the result as a literal of `target_type`. A null result becomes a
/// `NullLiteral`.
[[nodiscard]] arrow::Result<std::unique_ptr<ScalarExpression>> castConstant(
   const ScalarExpression& input,
   const schema::DataType& target_type
);

}  // namespace rhydb::query_engine::scalar_expressions
