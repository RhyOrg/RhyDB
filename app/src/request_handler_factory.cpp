#include "request_handler_factory.h"

#include <string>
#include <utility>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/URI.h>

#include "admin_query_handler.h"
#include "cors_handler.h"
#include "error_request_handler.h"
#include "health_handler.h"
#include "info_handler.h"
#include "lineage_definition_handler.h"
#include "logging_request_handler.h"
#include "not_found_handler.h"
#include "query_handler.h"
#include "request_id_handler.h"

namespace rhydb_app {

RhyDBRequestHandlerFactory::RhyDBRequestHandlerFactory(
   rhydb::config::RuntimeConfig runtime_config,
   std::optional<CorsPolicy> cors_policy,
   std::shared_ptr<ActiveDatabase> database_handle
)
    : runtime_config(std::move(runtime_config)),
      cors_policy(std::move(cors_policy)),
      database_handle(std::move(database_handle)) {}

Poco::Net::HTTPRequestHandler* RhyDBRequestHandlerFactory::createRequestHandler(
   const Poco::Net::HTTPServerRequest& request
) {
   std::unique_ptr<Poco::Net::HTTPRequestHandler> handler = std::make_unique<ErrorRequestHandler>(
      routeRequest(Poco::URI(request.getURI())), runtime_config
   );

   // CORS is an optional middleware: it is only inserted when a policy is configured.
   if (cors_policy.has_value()) {
      handler = std::make_unique<CorsHandler>(std::move(handler), cors_policy.value());
   }

   handler = std::make_unique<LoggingRequestHandler>(std::move(handler));

   return new RequestIdHandler(std::move(handler));
}

std::unique_ptr<Poco::Net::HTTPRequestHandler> RhyDBRequestHandlerFactory::routeRequest(
   const Poco::URI& uri
) {
   const std::string_view path = uri.getPath();
   std::vector<std::string> segments;
   uri.getPathSegments(segments);

   if (path == "/health") {
      return std::make_unique<HealthHandler>();
   }
   if (path == "/info") {
      return std::make_unique<InfoHandler>(database_handle);
   }
   if (segments.size() == 2 && segments.at(0) == "lineageDefinition") {
      return std::make_unique<LineageDefinitionHandler>(database_handle, segments.at(1));
   }
   if (path == "/query") {
      return std::make_unique<QueryHandler>(database_handle, runtime_config.query_options);
   }
   // opt-in write-enabled admin endpoint
   if (path == "/admin/query" && runtime_config.api_options.allow_admin_endpoint) {
      return std::make_unique<AdminQueryHandler>(
         database_handle,
         runtime_config.query_options,
         runtime_config.data_directory,
         admin_write_mutex
      );
   }
   return std::make_unique<NotFoundHandler>();
}

}  // namespace rhydb_app
