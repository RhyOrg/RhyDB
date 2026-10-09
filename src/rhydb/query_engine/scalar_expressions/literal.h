#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "rhydb/common/date32.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// A scalar literal value, e.g. `x := 3`. Literals are scalar expressions whose
/// value is known up front; their type() reflects the literal's column type.
/// They are not filter predicates, so compile() is only meaningful for the
/// boolean literal, which compiles to a Full (true) or Empty (false) operator.

class Int32Literal : public ScalarExpression {
  public:
   int32_t value;

   explicit Int32Literal(int32_t value);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::INT32; }

   static constexpr Kind KIND = Kind::INT32_LITERAL;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<Int32Literal>(value);
   }

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

class Int64Literal : public ScalarExpression {
  public:
   int64_t value;

   explicit Int64Literal(int64_t value);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::INT64; }

   static constexpr Kind KIND = Kind::INT64_LITERAL;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<Int64Literal>(value);
   }

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

class FloatLiteral : public ScalarExpression {
  public:
   double value;

   explicit FloatLiteral(double value);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::FLOAT; }

   static constexpr Kind KIND = Kind::FLOAT_LITERAL;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<FloatLiteral>(value);
   }

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

class StringLiteral : public ScalarExpression {
  public:
   std::string value;

   explicit StringLiteral(std::string value);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::STRING; }

   static constexpr Kind KIND = Kind::STRING_LITERAL;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<StringLiteral>(value);
   }

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

class BoolLiteral : public ScalarExpression {
  public:
   bool value;

   explicit BoolLiteral(bool value);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::BOOL; }

   static constexpr Kind KIND = Kind::BOOL_LITERAL;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<BoolLiteral>(value);
   }

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

class DateLiteral : public ScalarExpression {
  public:
   common::Date32 value;

   explicit DateLiteral(common::Date32 value);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::DATE32; }

   static constexpr Kind KIND = Kind::DATE_LITERAL;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<DateLiteral>(value);
   }

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
