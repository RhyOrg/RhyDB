//! Adds Cross-Origin Resource Sharing (CORS) support.
//!
//! This decorator wraps another handler. In wildcard mode (`*`) it sets a constant
//! `Access-Control-Allow-Origin: *` on every response, as the Fetch standard requires for such
//! resources, without a `Vary` header. In list mode it adds `Vary: Origin` to every response and,
//! for an allowed `Origin`, echoes it back in `Access-Control-Allow-Origin`. Preflight (`OPTIONS`)
//! requests from an allowed origin are answered directly with 204; preflights from a disallowed
//! origin get 403.

#pragma once

#include <memory>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include "cors_policy.h"

namespace rhydb_app {

class CorsHandler : public Poco::Net::HTTPRequestHandler {
   std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler;
   CorsPolicy cors_policy;

  public:
   CorsHandler(
      std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler,
      CorsPolicy cors_policy
   );

   void handleRequest(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override;

  private:
   /// Answers a preflight (OPTIONS) request: 204 with the CORS method/header allowances for an
   /// allowed origin, 403 for a disallowed one. In wildcard mode the caller has already set
   /// Access-Control-Allow-Origin, so every origin is allowed here.
   static void handlePreflight(
      const CorsPolicy& policy,
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   );
};

}  // namespace rhydb_app
