//! This class catches exceptions thrown from an object while it is
//! wrapped by it.

//! This is used by wrapping the object in question; it has a
//! `handleRequest` method and delegates that to the underlying
//! object, while catching exceptions.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <nlohmann/json.hpp>

#include <rhydb/config/runtime_config.h>

namespace rhydb_app {

struct ErrorResponse {
   std::string error;
   std::string message;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ErrorResponse, error, message);

/// Sends `error_response` as JSON body with a Content-Length. The status must be set before.
void sendErrorResponse(
   Poco::Net::HTTPServerResponse& response,
   const ErrorResponse& error_response
);

class ErrorRequestHandler : public Poco::Net::HTTPRequestHandler {
  private:
   std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler;
   const rhydb::config::RuntimeConfig& runtime_config;

  public:
   explicit ErrorRequestHandler(
      std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler,
      const rhydb::config::RuntimeConfig& runtime_config
   );

   void handleRequest(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override;

  private:
   /// Maps the exception that is currently being handled to an error response
   void respondToCurrentException(Poco::Net::HTTPServerResponse& response);

   std::optional<std::string> computeRetryAfterHintForStartupTime();
};

};  // namespace rhydb_app
