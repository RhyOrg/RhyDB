#pragma once

#include <arrow/compute/expression.h>

namespace rhydb::query_engine::exec_node {

/// Casts a `large_utf8` expression to `boolean`. Unlike Arrow's own string -> boolean cast, only
/// the exact (lower-case) spellings `true` and `false` are accepted; any other value fails the
/// execution with an `Invalid` status. Null stays null.
class StringToBoolExpression {
  public:
   static arrow::compute::Expression make(arrow::compute::Expression input_expression);
};

}  // namespace rhydb::query_engine::exec_node
