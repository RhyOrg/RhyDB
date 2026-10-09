#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

/// A scalar expression that zstd-decompresses the sequence-typed value its child expression
/// evaluates to into the STRING value it encodes. The child is usually a `FieldRef` to a compressed
/// column produced by the table scan. Used to expand the compressed columns of a table scan on
/// demand.
class ZstdDecompressScalar : public ScalarExpression {
  public:
   /// The expression producing the compressed input, usually a `FieldRef` to a scan column.
   std::unique_ptr<ScalarExpression> input;

   const std::string dictionary_string;

   ZstdDecompressScalar(std::unique_ptr<ScalarExpression> input, std::string dictionary_string);

   [[nodiscard]] schema::ColumnType type() const override { return schema::ColumnType::STRING; }

   static constexpr Kind KIND = Kind::ZSTD_DECOMPRESS_SCALAR;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override;

   [[nodiscard]] std::string toString() const override;

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
