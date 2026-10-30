#include "cors_handler.h"

#include <string>
#include <utility>

#include <Poco/Net/HTTPResponse.h>
#include <nlohmann/json.hpp>

#include "error_request_handler.h"

namespace rhydb_app {

namespace {

constexpr auto ORIGIN_HEADER = "Origin";
constexpr auto VARY_HEADER = "Vary";
constexpr auto ALLOW_ORIGIN_HEADER = "Access-Control-Allow-Origin";
constexpr auto EXPOSE_HEADERS_HEADER = "Access-Control-Expose-Headers";
constexpr auto REQUEST_METHOD_HEADER = "Access-Control-Request-Method";
constexpr auto REQUEST_HEADERS_HEADER = "Access-Control-Request-Headers";
constexpr auto ALLOW_METHODS_HEADER = "Access-Control-Allow-Methods";
constexpr auto ALLOW_HEADERS_HEADER = "Access-Control-Allow-Headers";
constexpr auto MAX_AGE_HEADER = "Access-Control-Max-Age";

// Headers that RhyDB sets on its responses and that browser JavaScript should be able to read.
constexpr auto EXPOSED_HEADERS = "X-Request-Id, data-version, result-ordering, Retry-After";
constexpr auto ALLOWED_METHODS = "GET, POST, OPTIONS";
// Chrome caps this at 2 hours; there is no reason to make it configurable.
constexpr auto MAX_AGE_SECONDS = "7200";

bool isPreflight(const Poco::Net::HTTPServerRequest& request) {
   return request.getMethod() == Poco::Net::HTTPRequest::HTTP_OPTIONS &&
          request.has(REQUEST_METHOD_HEADER);
}

}  // namespace

CorsHandler::CorsHandler(
   std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler,
   std::optional<rhydb::config::CorsPolicy> cors_policy
)
    : wrapped_handler(std::move(wrapped_handler)),
      cors_policy(std::move(cors_policy)) {}

void CorsHandler::handleRequest(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   // CORS disabled, or a non-browser request without an Origin: pass through unchanged.
   if (!cors_policy.has_value() || !request.has(ORIGIN_HEADER)) {
      wrapped_handler->handleRequest(request, response);
      return;
   }

   const auto& policy = cors_policy.value();
   const std::string origin = request.get(ORIGIN_HEADER);

   // In list mode the response depends on the Origin, so caches must key on it. This holds even
   // when the origin is not allowed (and thus gets no Allow-Origin header).
   if (!policy.allowsAnyOrigin()) {
      response.add(VARY_HEADER, ORIGIN_HEADER);
   }

   if (isPreflight(request)) {
      handlePreflight(policy, origin, request, response);
      return;
   }

   if (policy.isAllowed(origin)) {
      setAllowOriginHeaders(policy, origin, response);
      response.set(EXPOSE_HEADERS_HEADER, EXPOSED_HEADERS);
   }

   wrapped_handler->handleRequest(request, response);
}

void CorsHandler::setAllowOriginHeaders(
   const rhydb::config::CorsPolicy& policy,
   const std::string& origin,
   Poco::Net::HTTPServerResponse& response
) {
   if (policy.allowsAnyOrigin()) {
      response.set(ALLOW_ORIGIN_HEADER, "*");
   } else {
      response.set(ALLOW_ORIGIN_HEADER, origin);
   }
}

void CorsHandler::handlePreflight(
   const rhydb::config::CorsPolicy& policy,
   const std::string& origin,
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   if (!policy.isAllowed(origin)) {
      response.setContentType("application/json");
      response.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
      response.send() << nlohmann::json(
         ErrorResponse{.error = "Forbidden", .message = "Origin " + origin + " is not allowed"}
      );
      return;
   }

   setAllowOriginHeaders(policy, origin, response);
   response.set(ALLOW_METHODS_HEADER, ALLOWED_METHODS);
   // The browser enforces the actual request headers; we just reflect what it asked for.
   if (request.has(REQUEST_HEADERS_HEADER)) {
      response.set(ALLOW_HEADERS_HEADER, request.get(REQUEST_HEADERS_HEADER));
   }
   response.set(MAX_AGE_HEADER, MAX_AGE_SECONDS);

   response.setStatus(Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
   response.setContentLength(0);
   response.send();
}

}  // namespace rhydb_app
