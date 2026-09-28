#include "not_found_handler.h"

#include <string>

#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <nlohmann/json.hpp>

#include "error_request_handler.h"

namespace rhydb_app {
void NotFoundHandler::handleRequest(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   response.setContentType("application/json");
   response.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
   std::ostream& out_stream = response.send();
   out_stream << nlohmann::json(ErrorResponse{
      .error = "Not found", .message = "Resource " + request.getURI() + " does not exist",
   });
}
}  // namespace rhydb_app