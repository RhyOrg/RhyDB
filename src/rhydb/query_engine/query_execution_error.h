#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <arrow/status.h>

namespace rhydb::query_engine {

/// Attached to an `arrow::Status` to mark a run-time failure that is caused by the user's query or
/// data rather than by a bug in RhyDB. The canonical example is a type cast that cannot convert a
/// column value (e.g. `'abc'::int32`): Arrow only discovers this while executing the plan, so the
/// error surfaces as a plain `arrow::Status`. Tagging the status with this detail lets the boundary
/// (`QueryPlan::executeAndWrite`) tell such a user error apart from an internal "invalid plan" bug
/// and report it as a bad request instead of an internal server error.
///
/// The detail survives being carried through the Acero plan's final status, so it can be inspected
/// after `ExecPlan::finished()`.
class QueryExecutionErrorDetail : public arrow::StatusDetail {
  public:
   static constexpr const char* TYPE_ID = "rhydb::query_engine::QueryExecutionErrorDetail";

   [[nodiscard]] const char* type_id() const override;
   [[nodiscard]] std::string ToString() const override;
};

/// Wraps `status` so that it is recognized as a user/query error by `isQueryExecutionError`.
/// `status` must not be OK.
arrow::Status markAsQueryExecutionError(const arrow::Status& status);

/// Whether `status` was tagged with `QueryExecutionErrorDetail`.
bool isQueryExecutionError(const arrow::Status& status);

/// Thrown when a query fails during execution because of the user's query or data (as opposed to an
/// internal RhyDB error). Carries only the underlying message, with no "notify developers" wrapper.
class QueryExecutionException : public std::runtime_error {
  public:
   explicit QueryExecutionException(const std::string& error_message);
};

}  // namespace rhydb::query_engine
