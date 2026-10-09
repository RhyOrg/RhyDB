#include "rhydb_directory_watcher.h"

#include <cxxabi.h>
#include <optional>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <rhydb/common/data_version.h>
#include <rhydb/database.h>

#include "active_database.h"

rhydb_app::RhyDBDirectoryWatcher::RhyDBDirectoryWatcher(
   rhydb::RhyDBDirectory rhydb_directory,
   std::shared_ptr<ActiveDatabase> database_handle
)
    : rhydb_directory(std::move(rhydb_directory)),
      database_handle(std::move(database_handle)),
      timer(0, 2000) {
   timer.start(
      Poco::TimerCallback<RhyDBDirectoryWatcher>(
         *this, &RhyDBDirectoryWatcher::checkDirectoryForData
      )
   );
}

void rhydb_app::RhyDBDirectoryWatcher::checkDirectoryForData(Poco::Timer& /*timer*/) {
   auto maybe_most_recent_database_state = rhydb_directory.getMostRecentDataDirectory();

   if (maybe_most_recent_database_state == std::nullopt) {
      SPDLOG_INFO("No data found in {} for ingestion", rhydb_directory);
      return;
   }
   const auto& most_recent_database_state = maybe_most_recent_database_state.value();

   {
      try {
         const auto current_data_version_timestamp =
            database_handle->getActiveDatabase()->getDataVersionTimestamp();
         const auto most_recent_data_version_timestamp_found =
            most_recent_database_state.data_version.getTimestamp();
         if (current_data_version_timestamp >= most_recent_data_version_timestamp_found) {
            SPDLOG_TRACE(
               "Do not update data version to {} in path {}. Its version is not newer than the "
               "current version",
               most_recent_database_state.data_version.toString(),
               most_recent_database_state.path.string()
            );
            return;
         }
      } catch (const rhydb_app::UninitializedDatabaseException& exception) {
         SPDLOG_DEBUG("No database loaded yet - loading initial database next.");
      }
   }

   SPDLOG_INFO("New data version detected: {}", most_recent_database_state.path.string());
   try {
      database_handle->setActiveDatabase(
         rhydb::Database::loadDatabaseState(most_recent_database_state)
      );
      SPDLOG_INFO(
         "New database with version {} successfully loaded.",
         most_recent_database_state.path.string()
      );
      return;
   } catch (const std::exception& ex) {
      SPDLOG_ERROR(ex.what());
   } catch (const std::string& ex) {
      SPDLOG_ERROR(ex);
   } catch (...) {
      SPDLOG_ERROR("Loading cancelled with uncatchable (...) exception");
      if (const auto exception = std::current_exception()) {
         const auto* message = abi::__cxa_current_exception_type()->name();
         SPDLOG_ERROR("current_exception: {}", message);
      }
   }
   SPDLOG_INFO(
      "Did not load new database with version {} successfully.",
      most_recent_database_state.data_version.toString()
   );
}