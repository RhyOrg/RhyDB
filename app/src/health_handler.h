#pragma once

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include "rest_resource.h"

namespace rhydb_app {

class HealthHandler : public RestResource {
  public:
   explicit HealthHandler() = default;

   void get(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override;
};
}  // namespace rhydb_app
