#pragma once

#include <memory>
#include <mutex>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/URI.h>

#include <rhydb/config/runtime_config.h>

#include "active_database.h"
#include "error_request_handler.h"

namespace rhydb_app {

class RhyDBRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
   const rhydb::config::RuntimeConfig runtime_config;
   std::shared_ptr<ActiveDatabase> database_handle;
   std::shared_ptr<std::mutex> admin_write_mutex = std::make_shared<std::mutex>();

  public:
   RhyDBRequestHandlerFactory(
      rhydb::config::RuntimeConfig runtime_config,
      std::shared_ptr<ActiveDatabase> database_handle
   );

   Poco::Net::HTTPRequestHandler* createRequestHandler(
      const Poco::Net::HTTPServerRequest& request
   ) override;

   std::unique_ptr<Poco::Net::HTTPRequestHandler> routeRequest(const Poco::URI& uri);
};

}  // namespace rhydb_app
