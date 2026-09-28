#include "query_handler.h"

#include <string>
#include <utility>
#include <variant>

#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/StreamCopier.h>
#include <spdlog/spdlog.h>

#include <rhydb/query_engine/command/write_command.h>
#include <rhydb/query_engine/exec_node/arrow_ipc_sink.h>
#include <rhydb/query_engine/exec_node/ndjson_sink.h>
#include <rhydb/query_engine/illegal_query_exception.h>
#include <rhydb/query_engine/planner.h>
#include <rhydb/query_engine/saneql/parse_exception.h>
#include <evobench/evobench.hpp>

#include "active_database.h"
#include "bad_request.h"
#include "error_request_handler.h"

namespace rhydb_app {

QueryHandler::QueryHandler(
   std::shared_ptr<ActiveDatabase> database_handle,
   rhydb::config::QueryOptions query_options
)
    : query_options(query_options),
      database_handle(std::move(database_handle)) {}

namespace {

const uint64_t DEFAULT_TIMEOUT_TWO_MINUTES = 120;

}

void QueryHandler::post(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   EVOBENCH_SCOPE("QueryHandler", "post");

   // This fixes the database to outlive the execution of the query
   const auto database = database_handle->getActiveDatabase();

   const auto request_id = response.get("X-Request-Id");

   std::string query_string;
   std::istream& istream = request.stream();

   // TODO(#1244) add size limit for query_strings;
   Poco::StreamCopier::copyToString(istream, query_string);

   SPDLOG_INFO("Request Id [{}] - received query: {}", request_id, query_string);

   try {
      auto parsed_request =
         rhydb::query_engine::command::parseRequest(query_string, database->tables);

      // The read query endpoint must not mutate the database. A write statement (e.g. `insertInto`)
      // parses to a WriteCommand and has to go through the dedicated `POST /admin/query` endpoint.
      auto* query_node = std::get_if<rhydb::query_engine::operators::QueryNodePtr>(&parsed_request);
      if (query_node == nullptr) {
         throw BadRequest(
            "this query writes to the database; use the 'POST /admin/query' endpoint for write "
            "statements"
         );
      }

      auto query_plan = rhydb::query_engine::Planner::planQuery(
         std::move(*query_node), database->tables, query_options, request_id
      );

      response.set("data-version", database->getDataVersionTimestamp().value);
      response.set(
         "result-ordering", rhydb::query_engine::serializeResultOrdering(query_plan.result_ordering)
      );

      const std::string accept_header = request.has("Accept") ? request.get("Accept") : "";
      const bool use_arrow_ipc =
         accept_header.find("application/vnd.apache.arrow.stream") != std::string::npos;

      if (use_arrow_ipc) {
         response.setContentType("application/vnd.apache.arrow.stream");
         std::ostream& output_stream = response.send();
         auto result = rhydb::query_engine::exec_node::ArrowIpcSink::make(
            &output_stream, query_plan.results_schema
         );
         if (!result.ok()) {
            throw std::runtime_error(result.status().ToString());
         }
         auto output_sink = std::move(result).ValueUnsafe();

         EVOBENCH_SCOPE("QueryPlan", "executeAndWrite");
         query_plan.executeAndWrite(output_sink, DEFAULT_TIMEOUT_TWO_MINUTES);
      } else {
         response.setContentType("application/x-ndjson");
         std::ostream& output_stream = response.send();
         rhydb::query_engine::exec_node::NdjsonSink output_sink{
            &output_stream, query_plan.results_schema,
         };

         EVOBENCH_SCOPE("QueryPlan", "executeAndWrite");
         query_plan.executeAndWrite(output_sink, DEFAULT_TIMEOUT_TWO_MINUTES);
      }
   } catch (const rhydb::query_engine::saneql::ParseException& ex) {
      throw BadRequest(ex.what());
   } catch (const rhydb::query_engine::IllegalQueryException& ex) {
      throw BadRequest(ex.what());
   }
}

}  // namespace rhydb_app
