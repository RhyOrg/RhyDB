#include "admin_query_handler.h"

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <rhydb/common/rhydb_directory.h>
#include <rhydb/database.h>
#include <rhydb/query_engine/command/write_command.h>
#include <evobench/evobench.hpp>

#include "active_database.h"
#include "query_response.h"

namespace rhydb_app {

AdminQueryHandler::AdminQueryHandler(
   std::shared_ptr<ActiveDatabase> database_handle,
   rhydb::config::QueryOptions query_options,
   std::filesystem::path data_directory,
   std::shared_ptr<std::mutex> write_mutex
)
    : query_options(query_options),
      data_directory(std::move(data_directory)),
      database_handle(std::move(database_handle)),
      write_mutex(std::move(write_mutex)) {}

rhydb::Database AdminQueryHandler::loadDatabaseToWriteTo() const {
   EVOBENCH_SCOPE("AdminQueryHandler", "loadDatabaseToWriteTo");

   const auto data_source = rhydb::RhyDBDirectory{data_directory}.getMostRecentDataDirectory();
   if (!data_source.has_value()) {
      throw std::runtime_error(fmt::format(
         "the data directory '{}' holds no loadable database state to apply the write to",
         data_directory.string()
      ));
   }

   const auto served_version = database_handle->getActiveDatabase()->getDataVersionTimestamp();
   const auto version_to_write_to = data_source->data_version.getTimestamp();
   if (version_to_write_to < served_version) {
      throw std::runtime_error(fmt::format(
         "the most recent state in the data directory '{}' has data version {}, which is older "
         "than the data version {} that is being served. Please resolve this conflict manually",
         data_directory.string(),
         version_to_write_to.value,
         served_version.value
      ));
   }
   if (served_version < version_to_write_to) {
      SPDLOG_WARN(
         "Admin write applies to data version {} from the data directory, which is newer than the "
         "data version {} currently being served",
         version_to_write_to.value,
         served_version.value
      );
   }

   return rhydb::Database::loadDatabaseState(data_source.value());
}

void AdminQueryHandler::post(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   EVOBENCH_SCOPE("AdminQueryHandler", "post");

   const auto request_id = response.get("X-Request-Id");

   const std::string query_string = readQueryString(request, request_id);

   rethrowInvalidQueryAsBadRequest([&] {
      const auto [write_result, data_version] = [&] {
         // One write at a time
         const std::scoped_lock<std::mutex> write_lock{*write_mutex};

         rhydb::Database staged_database = loadDatabaseToWriteTo();

         auto result = staged_database.executeWrite(query_string, query_options, request_id);

         staged_database.saveDatabaseState(data_directory);

         return std::pair{std::move(result), staged_database.getDataVersionTimestamp()};
      }();

      SPDLOG_INFO(
         "Request Id [{}] - admin write statement applied; new data version {}",
         request_id,
         data_version.value
      );

      sendQueryResult(
         request,
         response,
         QueryResult{
            .data_version = data_version,
            .schema = write_result->schema(),
            .write_to_sink =
               [&](rhydb::query_engine::exec_node::ArrowBatchSink& output_sink) {
                  return rhydb::query_engine::command::writeToSink(*write_result, output_sink);
               },
         }
      );
   });
}

}  // namespace rhydb_app
