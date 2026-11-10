#pragma once

#include <memory>

#include <arrow/compute/cast.h>
#include <arrow/compute/expression.h>
#include <arrow/type.h>

namespace rhydb::query_engine::exec_node {

/// Casts `input_expression` to `target_type` like Arrow's own `cast`, but tags a run-time failure
/// (e.g. an out-of-range or unparseable column value) with `QueryExecutionErrorDetail`. This lets
/// the execution boundary report such a failure as a user/query error instead of an internal bug.
///
/// `options` carries the target type and the (safe) cast behavior. The target type must be one of
/// the plain output types RhyDB casts to (string, date32, boolean, int32, int64, float64).
class TaggedCastExpression {
  public:
   static arrow::compute::Expression make(
      arrow::compute::Expression input_expression,
      const std::shared_ptr<arrow::DataType>& target_type
   );
};

}  // namespace rhydb::query_engine::exec_node
