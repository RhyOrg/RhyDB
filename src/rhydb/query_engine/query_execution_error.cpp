#include "rhydb/query_engine/query_execution_error.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine {

const char* QueryExecutionErrorDetail::type_id() const {
   return TYPE_ID;
}

std::string QueryExecutionErrorDetail::ToString() const {
   return "query execution error";
}

arrow::Status markAsQueryExecutionError(const arrow::Status& status) {
   RHYDB_ASSERT(!status.ok());
   return status.WithDetail(std::make_shared<QueryExecutionErrorDetail>());
}

bool isQueryExecutionError(const arrow::Status& status) {
   const auto& detail = status.detail();
   return detail != nullptr &&
          std::string_view{detail->type_id()} == QueryExecutionErrorDetail::TYPE_ID;
}

QueryExecutionException::QueryExecutionException(const std::string& error_message)
    : std::runtime_error(error_message) {}

}  // namespace rhydb::query_engine
