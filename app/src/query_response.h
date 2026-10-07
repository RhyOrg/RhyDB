#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <arrow/compute/ordering.h>
#include <arrow/status.h>
#include <arrow/type_fwd.h>

#include <rhydb/common/data_version.h>
#include <rhydb/query_engine/exec_node/arrow_batch_sink.h>

namespace rhydb_app {

/// The shared request/response handling of the query endpoints (`POST /query` and
/// `POST /admin/query`), so that both behave identically towards clients in terms of request
/// parsing, error mapping, response headers and response formats.

/// Reads the query string from the request body and logs it.
[[nodiscard]] std::string readQueryString(
   Poco::Net::HTTPServerRequest& request,
   std::string_view request_id
);

/// Runs `handle_query`, rethrowing the exceptions caused by an invalid query as BadRequest.
void rethrowInvalidQueryAsBadRequest(const std::function<void()>& handle_query);

/// The result of a query, as it is sent back to the client.
struct QueryResult {
   rhydb::DataVersion::Timestamp data_version;
   arrow::compute::Ordering result_ordering = arrow::compute::Ordering::Unordered();
   std::shared_ptr<arrow::Schema> schema;
   /// Writes the result batches to the sink and finishes it.
   std::function<arrow::Status(rhydb::query_engine::exec_node::ArrowBatchSink&)> write_to_sink;
};

/// Sets the `data-version` and `result-ordering` headers and streams the result in the format the
/// `Accept` header asks for: Arrow IPC stream for `application/vnd.apache.arrow.stream`, NDJSON
/// otherwise.
void sendQueryResult(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response,
   const QueryResult& query_result
);

}  // namespace rhydb_app
