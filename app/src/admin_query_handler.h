#pragma once

#include <filesystem>
#include <memory>
#include <mutex>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include <rhydb/config/runtime_config.h>
#include <rhydb/database.h>

#include "active_database.h"
#include "rest_resource.h"

namespace rhydb_app {

/// Handles write-enabled endpoint `POST /admin/query`.
/// Only routed to when `api.allowAdminEndpoint` is enabled.
class AdminQueryHandler : public RestResource {
   rhydb::config::QueryOptions query_options;
   std::filesystem::path data_directory;
   std::shared_ptr<ActiveDatabase> database_handle;
   std::shared_ptr<std::mutex> write_mutex;

  public:
   AdminQueryHandler(
      std::shared_ptr<ActiveDatabase> database_handle,
      rhydb::config::QueryOptions query_options,
      std::filesystem::path data_directory,
      std::shared_ptr<std::mutex> write_mutex
   );

   void post(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override;

  private:
   [[nodiscard]] rhydb::Database loadDatabaseToWriteTo() const;
};

}  // namespace rhydb_app
