#pragma once

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include "active_database.h"
#include "rest_resource.h"

namespace rhydb_app {

class InfoHandler : public RestResource {
  private:
   std::shared_ptr<ActiveDatabase> database_handle;

  public:
   explicit InfoHandler(std::shared_ptr<ActiveDatabase> database_handle);

   void get(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override;
};
}  // namespace rhydb_app
