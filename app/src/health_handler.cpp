#include "health_handler.h"

#include <map>
#include <string>
#include <string_view>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/URI.h>

namespace rhydb_app {

void HealthHandler::get(
   Poco::Net::HTTPServerRequest& /*request*/,
   Poco::Net::HTTPServerResponse& response
) {
   response.setContentType("application/json");
   response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
   constexpr std::string_view BODY = R"({"status":"UP"})";
   response.sendBuffer(BODY.data(), BODY.size());
}
}  // namespace rhydb_app
