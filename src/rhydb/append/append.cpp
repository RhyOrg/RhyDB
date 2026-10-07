#include "rhydb/append/append.h"

#include "rhydb/append/table_inserter.h"
#include "rhydb/common/input_stream_wrapper.h"
#include "rhydb/common/rhydb_directory.h"
#include "rhydb/database.h"

using rhydb::Database;
using rhydb::RhyDBDataSource;
using rhydb::RhyDBDirectory;
using rhydb::config::AppendConfig;

class AppendError : public std::runtime_error {
  public:
   explicit AppendError(const std::string& error_message)
       : std::runtime_error(error_message) {}
};

namespace {

RhyDBDataSource getMostRecentOrSpecifiedDatabaseState(
   const RhyDBDirectory& rhydb_directory,
   const std::optional<std::filesystem::path>& specified_directory
) {
   if (specified_directory.has_value()) {
      return RhyDBDataSource::checkValidDataSource(specified_directory.value());
   }
   SPDLOG_INFO(
      "No data directory specified, automatically using the most recent one in the data directory "
      "{}",
      rhydb_directory
   );
   auto most_recent_data_directory = rhydb_directory.getMostRecentDataDirectory();
   if (most_recent_data_directory == std::nullopt) {
      throw AppendError{
         "No data directory specified and the data directory does not contain any valid data "
         "source.",
      };
   }
   return most_recent_data_directory.value();
}

}  // namespace

namespace rhydb::append {

int runAppend(const AppendConfig& append_config) {
   const RhyDBDirectory data_directory{append_config.data_directory};

   const auto database_state_directory =
      getMostRecentOrSpecifiedDatabaseState(data_directory, append_config.data_source);

   SPDLOG_INFO("append - Loading database from {}", database_state_directory.path);
   Database database = Database::loadDatabaseState(database_state_directory);

   SPDLOG_INFO("append - appending data to the database");
   auto input = InputStreamWrapper::openFileOrStdIn(append_config.append_file);
   database.appendData(schema::TableName::getDefault(), input.getInputStream());

   SPDLOG_INFO("append - saving database to directory '{}'", append_config.data_directory);
   database.saveDatabaseState(append_config.data_directory);

   SPDLOG_INFO(
      "append - finished appending data, resulting database info: {}", database.getDatabaseInfo()
   );

   return 0;
}

}  // namespace rhydb::append
