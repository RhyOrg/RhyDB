#include "info_handler.h"

#include <string>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/URI.h>
#include <nlohmann/json.hpp>
#include <utility>

#include "active_database.h"

namespace rhydb_app {

InfoHandler::InfoHandler(std::shared_ptr<ActiveDatabase> database_handle)
    : database_handle(std::move(database_handle)) {}

void InfoHandler::get(
   Poco::Net::HTTPServerRequest& /*request*/,
   Poco::Net::HTTPServerResponse& response
) {
   const auto database = database_handle->getActiveDatabase();

   response.set("data-version", database->getDataVersionTimestamp().value);

   const std::string database_info = nlohmann::json(database->getDatabaseInfo()).dump();
   response.setContentType("application/json");
   response.sendBuffer(database_info.data(), database_info.size());
}
}  // namespace rhydb_app
