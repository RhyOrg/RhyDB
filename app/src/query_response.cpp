#include "query_response.h"

#include <stdexcept>
#include <string>
#include <utility>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/StreamCopier.h>
#include <spdlog/spdlog.h>

#include <rhydb/append/append_exception.h>
#include <rhydb/query_engine/exec_node/arrow_ipc_sink.h>
#include <rhydb/query_engine/exec_node/ndjson_sink.h>
#include <rhydb/query_engine/illegal_query_exception.h>
#include <rhydb/query_engine/query_plan.h>
#include <rhydb/query_engine/saneql/parse_exception.h>
#include <evobench/evobench.hpp>

#include "bad_request.h"

namespace rhydb_app {

std::string readQueryString(Poco::Net::HTTPServerRequest& request, std::string_view request_id) {
   std::string query_string;
   std::istream& istream = request.stream();

   // TODO(#1244) add size limit for query_strings;
   Poco::StreamCopier::copyToString(istream, query_string);

   SPDLOG_INFO(
      "Request Id [{}] - received query on {}: {}", request_id, request.getURI(), query_string
   );

   return query_string;
}

void rethrowInvalidQueryAsBadRequest(const std::function<void()>& handle_query) {
   try {
      handle_query();
   } catch (const rhydb::query_engine::saneql::ParseException& ex) {
      throw BadRequest(ex.what());
   } catch (const rhydb::query_engine::IllegalQueryException& ex) {
      throw BadRequest(ex.what());
   } catch (const rhydb::append::AppendException& ex) {
      throw BadRequest(ex.what());
   }
}

namespace {

void writeResult(
   const QueryResult& query_result,
   rhydb::query_engine::exec_node::ArrowBatchSink& output_sink,
   std::string_view request_id
) {
   EVOBENCH_SCOPE("QueryResult", "writeToSink");
   const auto status = query_result.write_to_sink(output_sink);
   if (status.ok()) {
      return;
   }
   if (status.IsIOError()) {
      SPDLOG_WARN(
         "The request {} encountered an IO Error when sending the response. We expect that the "
         "user cancelled the request while the response was sent and ignore the error",
         request_id
      );
      return;
   }
   throw std::runtime_error(status.ToString());
}

}  // namespace

void sendQueryResult(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response,
   const QueryResult& query_result
) {
   const auto request_id = response.get("X-Request-Id");

   response.set("data-version", query_result.data_version.value);
   response.set(
      "result-ordering", rhydb::query_engine::serializeResultOrdering(query_result.result_ordering)
   );

   const std::string accept_header = request.has("Accept") ? request.get("Accept") : "";
   const bool use_arrow_ipc = accept_header.contains("application/vnd.apache.arrow.stream");

   if (use_arrow_ipc) {
      response.setContentType("application/vnd.apache.arrow.stream");
      std::ostream& output_stream = response.send();
      auto output_sink =
         rhydb::query_engine::exec_node::ArrowIpcSink::make(&output_stream, query_result.schema);
      if (!output_sink.ok()) {
         throw std::runtime_error(output_sink.status().ToString());
      }
      writeResult(query_result, *output_sink, request_id);
   } else {
      response.setContentType("application/x-ndjson");
      std::ostream& output_stream = response.send();
      rhydb::query_engine::exec_node::NdjsonSink output_sink{&output_stream, query_result.schema};
      writeResult(query_result, output_sink, request_id);
   }
}

}  // namespace rhydb_app
