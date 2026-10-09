#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// Comparison predicate: `left <op> right`, for every comparison operator the
/// query language offers (`=`, `<>`, `<`, `<=`, `>`, `>=`). One side is a column
/// reference (FieldRef) and the other a literal value; compile() recognises this
/// "column <op> constant" shape and lowers it to an efficient filter, dispatching
/// on the literal's type.
///
/// `null` is not a comparable value and never matches.
///
/// Ordering operators are rejected for boolean columns; `=` and `<>` support all
/// column types.
class Comparison : public ScalarExpression {
   std::unique_ptr<ScalarExpression> left;
   std::unique_ptr<ScalarExpression> right;
   filter::operators::Comparator comparator;

  public:
   Comparison(
      std::unique_ptr<ScalarExpression> left,
      std::unique_ptr<ScalarExpression> right,
      filter::operators::Comparator comparator
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<Comparison>(left->clone(), right->clone(), comparator);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::COMPARISON;
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
