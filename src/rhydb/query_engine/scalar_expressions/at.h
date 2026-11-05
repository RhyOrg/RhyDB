#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// A scalar expression that evaluates to the single character at a given 1-indexed position of the
/// string its child expression evaluates to, e.g. `seq.at(3)`. The child is usually a `FieldRef` to
/// a string column, but may be any string-valued expression (e.g. a `ZstdDecompressScalar` after a
/// map merge). Its value is a string (the character), so it is not a filter predicate itself and
/// compile() is unimplemented. Comparisons of a sequence position against a symbol are rewritten
/// into sequence-index filters by `Comparison::rewrite`.
class At : public ScalarExpression {
  public:
   std::unique_ptr<ScalarExpression> input;
   /// 1-indexed position of the character to extract.
   uint32_t position;

   At(std::unique_ptr<ScalarExpression> input, uint32_t position);

   [[nodiscard]] schema::DataType type() const override { return schema::DataType::STRING; }

   static constexpr Kind KIND = Kind::AT;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] arrow::Result<arrow::compute::Expression> toArrowExpression() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<At>(input->clone(), position);
   }

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table,
      bool with_null_rows
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
