#include "error_request_handler.h"

#include <exception>
#include <typeinfo>

#include <cxxabi.h>

#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "active_database.h"
#include "bad_request.h"

namespace rhydb_app {
ErrorRequestHandler::ErrorRequestHandler(
   std::unique_ptr<Poco::Net::HTTPRequestHandler> wrapped_handler,
   const rhydb::config::RuntimeConfig& runtime_config
)
    : wrapped_handler(std::move(wrapped_handler)),
      runtime_config(runtime_config) {}

void ErrorRequestHandler::handleRequest(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response
) {
   try {
      wrapped_handler->handleRequest(request, response);
   } catch (const rhydb_app::UninitializedDatabaseException& exception) {
      SPDLOG_INFO("Caught exception: {}", exception.what());

      response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
      std::string message = "Database not initialized yet.";

      const auto retry_after = computeRetryAfterHintForStartupTime();
      if (retry_after.has_value()) {
         response.set("Retry-After", retry_after.value());
         message += " Please try again after " + retry_after.value() + " seconds.";
      }
      std::ostream& out_stream = response.send();
      out_stream << nlohmann::json(
         ErrorResponse{.error = "Service Temporarily Unavailable", .message = message}
      );
   } catch (const rhydb_app::BadRequest& exception) {
      response.setContentType("application/json");
      response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
      std::ostream& out_stream = response.send();
      out_stream << nlohmann::json(
         ErrorResponse{.error = "Bad request", .message = exception.what()}
      );
   } catch (const std::exception& exception) {
      SPDLOG_ERROR("Caught exception: {}", exception.what());

      response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
      std::ostream& out_stream = response.send();
      out_stream << nlohmann::json(
         ErrorResponse{.error = "Internal Server Error", .message = exception.what()}
      );
   } catch (const std::string& ex) {
      SPDLOG_ERROR(ex);
      response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
      std::ostream& out_stream = response.send();
      out_stream << nlohmann::json(ErrorResponse{.error = "Internal Server Error", .message = ex});
   } catch (...) {
      SPDLOG_ERROR("Query cancelled with uncatchable (...) exception");
      response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
      std::ostream& out_stream = response.send();

      const auto exception = std::current_exception();
      if (exception) {
         const auto* message = abi::__cxa_current_exception_type()->name();
         SPDLOG_ERROR("current_exception: {}", message);
         out_stream << nlohmann::json(
            ErrorResponse{.error = "Internal Server Error", .message = message}
         );
      } else {
         out_stream << nlohmann::json(ErrorResponse{
            .error = "Internal Server Error", .message = "non recoverable error message",
         });
      }
   }
}

std::optional<std::string> ErrorRequestHandler::computeRetryAfterHintForStartupTime() {
   if (!runtime_config.api_options.estimated_startup_end.has_value()) {
      return std::nullopt;
   }

   const auto now = std::chrono::system_clock::now();

   const auto startup_time_end = runtime_config.api_options.estimated_startup_end.value();

   const auto remaining_startup_time_in_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(startup_time_end - now).count();

   if (remaining_startup_time_in_seconds <= 0) {
      return std::nullopt;
   }

   return std::to_string(remaining_startup_time_in_seconds);
}

}  // namespace rhydb_app
