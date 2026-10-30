//! Adds Cross-Origin Resource Sharing (CORS) support.
//!
//! This decorator wraps another handler. When CORS is enabled (a policy is set) and the request
//! carries an allowed `Origin`, it adds the CORS response headers before delegating. Preflight
//! (`OPTIONS`) requests from an allowed origin are answered directly with 204; preflights from a
//! disallowed origin get 403. When CORS is disabled, or there is no `Origin`, the request is passed
//! through unchanged.

#pragma once

#include <memory>
#include <optional>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include <rhydb/config/cors_policy.h>

namespace rhydb_app {

class CorsHandler : public Poco::Net::HTTPRequestHandler {
   std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler;
   std::optional<rhydb::config::CorsPolicy> cors_policy;

  public:
   CorsHandler(
      std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler,
      std::optional<rhydb::config::CorsPolicy> cors_policy
   );

   void handleRequest(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override;

  private:
   /// Sets Access-Control-Allow-Origin (and Vary: Origin in list mode) for an allowed origin.
   void setAllowOriginHeaders(
      const rhydb::config::CorsPolicy& policy,
      const std::string& origin,
      Poco::Net::HTTPServerResponse& response
   );

   void handlePreflight(
      const rhydb::config::CorsPolicy& policy,
      const std::string& origin,
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   );
};

}  // namespace rhydb_app
