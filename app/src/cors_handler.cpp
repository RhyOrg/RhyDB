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
   std::optional<CorsPolicy> cors_policy
)
    : wrapped_handler(std::move(wrapped_handler)),
      cors_policy(std::move(cors_policy)) {}

void CorsHandler::handleRequest(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   // CORS disabled: pass through unchanged, without any CORS semantics.
   if (!cors_policy.has_value()) {
      wrapped_handler->handleRequest(request, response);
      return;
   }

   const auto& policy = cors_policy.value();

   // Wildcard mode sends a constant `Access-Control-Allow-Origin: *` that does not depend on the
   // request's Origin. The Fetch standard requires such a resource to send that header on *every*
   // response, CORS or not, and to not use Vary; otherwise a cache keyed only on the URL is still
   // correct because the header is identical for every request.
   // See https://fetch.spec.whatwg.org/#cors-protocol-and-http-caches
   if (policy.allowsAnyOrigin()) {
      response.set(ALLOW_ORIGIN_HEADER, "*");
      response.set(EXPOSE_HEADERS_HEADER, EXPOSED_HEADERS);

      if (isPreflight(request)) {
         handlePreflight(policy, request, response);
         return;
      }

      wrapped_handler->handleRequest(request, response);
      return;
   }

   // In list mode the response depends on the Origin, so caches must key on it. This must be set
   // on every CORS-enabled response, including ones without an Origin or with a disallowed origin
   // (which get no Allow-Origin header); otherwise a cache could serve such a response to an
   // allowed origin, which the browser would then reject.
   response.add(VARY_HEADER, ORIGIN_HEADER);

   // A non-browser request without an Origin gets no CORS headers (but keeps Vary).
   if (!request.has(ORIGIN_HEADER)) {
      wrapped_handler->handleRequest(request, response);
      return;
   }

   const std::string origin = request.get(ORIGIN_HEADER);

   if (isPreflight(request)) {
      handlePreflight(policy, request, response);
      return;
   }

   if (policy.isAllowed(origin)) {
      response.set(ALLOW_ORIGIN_HEADER, origin);
      response.set(EXPOSE_HEADERS_HEADER, EXPOSED_HEADERS);
   }

   wrapped_handler->handleRequest(request, response);
}

void CorsHandler::handlePreflight(
   const CorsPolicy& policy,
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   // In wildcard mode Access-Control-Allow-Origin: * was already set by the caller and every
   // origin is allowed, so there is nothing more to check. In list mode we answer an allowed
   // origin by echoing it and reject anything else.
   if (!policy.allowsAnyOrigin()) {
      const std::string origin = request.get(ORIGIN_HEADER);
      if (!policy.isAllowed(origin)) {
         // The Origin is attacker-controlled and may contain bytes that are not valid UTF-8, which
         // would make serializing it into the JSON body throw. The CorsHandler runs outside the
         // ErrorRequestHandler, so such an exception would escape unhandled; therefore we do not
         // echo the Origin back into the response body.
         response.setContentType("application/json");
         response.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
         response.send() << nlohmann::json(
            ErrorResponse{.error = "Forbidden", .message = "Origin is not allowed"}
         );
         return;
      }
      response.set(ALLOW_ORIGIN_HEADER, origin);
   }

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
