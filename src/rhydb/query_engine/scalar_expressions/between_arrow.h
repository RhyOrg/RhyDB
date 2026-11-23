#pragma once

#include <optional>
#include <string>

#include <arrow/compute/api.h>
#include <arrow/datum.h>

namespace rhydb::query_engine::scalar_expressions {

/// The Arrow translation shared by the `between` expressions (`IntBetween`, `FloatBetween`,
/// `DateBetween`): `column` is within `[from, to]`, both bounds inclusive. An absent bound is
/// open. The bounds must be scalar datums that Arrow can compare to the column.
arrow::compute::Expression betweenToArrowExpression(
   const std::string& column_name,
   const std::optional<arrow::Datum>& from,
   const std::optional<arrow::Datum>& to
);

}  // namespace rhydb::query_engine::scalar_expressions
