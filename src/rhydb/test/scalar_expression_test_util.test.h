#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <arrow/api.h>
#include <arrow/compute/api.h>

#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::test {

/// Translates `expression` to an Arrow expression, binds it to `schema` and evaluates it on
/// `batch`. Dies if any step fails. A result that does not depend on the input (e.g. a literal) is
/// broadcast to one value per row of `batch`.
inline std::shared_ptr<arrow::Array> evaluateOnBatch(
   const query_engine::scalar_expressions::ScalarExpression& expression,
   const std::shared_ptr<arrow::Schema>& schema,
   const std::shared_ptr<arrow::RecordBatch>& batch
) {
   const auto arrow_expression = expression.toArrowExpression().ValueOrDie();
   const auto bound = arrow_expression.Bind(*schema).ValueOrDie();
   const auto value =
      arrow::compute::ExecuteScalarExpression(bound, arrow::compute::ExecBatch(*batch))
         .ValueOrDie();
   if (value.is_scalar()) {
      return arrow::MakeArrayFromScalar(*value.scalar(), batch->num_rows()).ValueOrDie();
   }
   return value.make_array();
}

inline std::shared_ptr<arrow::Array> makeLargeStringArray(
   const std::vector<std::optional<std::string>>& values
) {
   arrow::LargeStringBuilder builder;
   for (const auto& value : values) {
      if (value.has_value()) {
         (void)builder.Append(value.value());
      } else {
         (void)builder.AppendNull();
      }
   }
   return builder.Finish().ValueOrDie();
}

inline std::shared_ptr<arrow::Array> makeBooleanArray(
   const std::vector<std::optional<bool>>& values
) {
   arrow::BooleanBuilder builder;
   for (const auto& value : values) {
      if (value.has_value()) {
         (void)builder.Append(value.value());
      } else {
         (void)builder.AppendNull();
      }
   }
   return builder.Finish().ValueOrDie();
}

}  // namespace rhydb::test
