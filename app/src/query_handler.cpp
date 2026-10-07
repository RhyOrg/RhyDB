#include "query_handler.h"

#include <string>
#include <utility>
#include <variant>

#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <spdlog/spdlog.h>

#include <rhydb/query_engine/command/write_command.h>
#include <rhydb/query_engine/planner.h>
#include <evobench/evobench.hpp>

#include "active_database.h"
#include "bad_request.h"
#include "query_response.h"

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

   const std::string query_string = readQueryString(request, request_id);

   rethrowInvalidQueryAsBadRequest([&] {
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

      sendQueryResult(
         request,
         response,
         QueryResult{
            .data_version = database->getDataVersionTimestamp(),
            .result_ordering = query_plan.result_ordering,
            .schema = query_plan.results_schema,
            .write_to_sink =
               [&](rhydb::query_engine::exec_node::ArrowBatchSink& output_sink) {
                  EVOBENCH_SCOPE("QueryPlan", "executeAndWrite");
                  query_plan.executeAndWrite(output_sink, DEFAULT_TIMEOUT_TWO_MINUTES);
                  return arrow::Status::OK();
               },
         }
      );
   });
}

}  // namespace rhydb_app
