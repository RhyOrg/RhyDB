#include <Poco/Net/HTTPResponse.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "active_database.h"
#include "error_request_handler.h"
#include "manual_poco_mocks.test.h"

namespace {

class MockRequestHandler : public Poco::Net::HTTPRequestHandler {
  public:
   MOCK_METHOD(
      void,
      handleRequest,
      (Poco::Net::HTTPServerRequest & request, Poco::Net::HTTPServerResponse& response),
      ()
   );
};

const auto TEST_RUNTIME_CONFIG = [] {
   auto config = rhydb::config::RuntimeConfig::withDefaults();
   config.api_options.estimated_startup_end = std::chrono::system_clock::now();
   return config;
}();

rhydb::config::RuntimeConfig getRuntimeConfigThatEndsInXMinutes(
   std::chrono::minutes estimated_time_in_minutes
) {
   const std::chrono::time_point point = std::chrono::system_clock::now();
   auto result = rhydb::config::RuntimeConfig::withDefaults();
   result.api_options.estimated_startup_end = point + estimated_time_in_minutes;
   return result;
}

const int FOUR_MINUTES_IN_SECONDS = 240;
}  // namespace

// We want to test whether ErrorRequestHandler works, i.e. whether it
// catches an exception which is thrown, but wrapped by it.

TEST(ErrorRequestHandler, handlesRuntimeErrors) {
   auto wrapped_handler_mock = std::make_unique<MockRequestHandler>();
   ON_CALL(*wrapped_handler_mock, handleRequest)
      .WillByDefault(testing::Throw(std::runtime_error("test exception, expected to be caught")));

   auto under_test =
      rhydb_app::ErrorRequestHandler(std::move(wrapped_handler_mock), TEST_RUNTIME_CONFIG);

   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);
   under_test.handleRequest(request, response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
   EXPECT_EQ(
      response.out_stream.str(),
      R"({"error":"Internal Server Error","message":"test exception, expected to be caught"})"
   );
}

TEST(ErrorRequestHandler, handlesOtherErrors) {
   auto wrapped_handler_mock = std::make_unique<MockRequestHandler>();

   ON_CALL(*wrapped_handler_mock, handleRequest)
      .WillByDefault(testing::Throw(
         "One should not actually do this - since C++ admits it, throw a string here"
      ));

   auto under_test =
      rhydb_app::ErrorRequestHandler(std::move(wrapped_handler_mock), TEST_RUNTIME_CONFIG);

   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);
   under_test.handleRequest(request, response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
   EXPECT_EQ(response.out_stream.str(), R"({"error":"Internal Server Error","message":"PKc"})");
}

TEST(ErrorRequestHandler, doesNothingIfNoExceptionIsThrown) {
   const std::string_view wrapped_request_handler_message =
      "A message that the actual handler would write";
   auto wrapped_handler_mock = std::make_unique<MockRequestHandler>();

   EXPECT_CALL(*wrapped_handler_mock, handleRequest).Times(testing::AtLeast(1));

   auto under_test =
      rhydb_app::ErrorRequestHandler(std::move(wrapped_handler_mock), TEST_RUNTIME_CONFIG);

   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);

   response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
   response.send() << wrapped_request_handler_message;

   under_test.handleRequest(request, response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
   EXPECT_EQ(response.out_stream.str(), wrapped_request_handler_message);
}

TEST(
   ErrorRequestHandler,
   givenDuringStartupTime_whenIQueryUninitializedDatabase_thenReturnsRetryAfter
) {
   const std::string_view wrapped_request_handler_message =
      "A message that the actual handler would write";
   auto wrapped_handler_mock = std::make_unique<MockRequestHandler>();

   auto database = std::make_shared<rhydb_app::ActiveDatabase>();
   ON_CALL(*wrapped_handler_mock, handleRequest).WillByDefault([&] {
      database->getActiveDatabase();
      RHYDB_UNREACHABLE();
   });

   auto runtime_config = getRuntimeConfigThatEndsInXMinutes(std::chrono::minutes{5});

   auto under_test =
      rhydb_app::ErrorRequestHandler(std::move(wrapped_handler_mock), runtime_config);

   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);

   response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
   response.send() << wrapped_request_handler_message;

   under_test.handleRequest(request, response);

   const auto retry_after = std::stoi(response.get("Retry-After"));

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
   EXPECT_GT(retry_after, FOUR_MINUTES_IN_SECONDS);
   EXPECT_THAT(response.out_stream.str(), testing::HasSubstr("Database not initialized yet"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(
   ErrorRequestHandler,
   givenStartupTimeIsOver_whenIQueryUninitializedDatabase_thenReturnsErrorWithoutRetryAfter
) {
   const std::string_view wrapped_request_handler_message =
      "A message that the actual handler would write";
   auto wrapped_handler_mock = std::make_unique<MockRequestHandler>();

   auto database = std::make_shared<rhydb_app::ActiveDatabase>();
   ON_CALL(*wrapped_handler_mock, handleRequest).WillByDefault([&] {
      database->getActiveDatabase();
      RHYDB_UNREACHABLE();
   });

   auto runtime_config = getRuntimeConfigThatEndsInXMinutes(std::chrono::minutes{-4});

   auto under_test =
      rhydb_app::ErrorRequestHandler(std::move(wrapped_handler_mock), runtime_config);

   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);

   response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
   response.send() << wrapped_request_handler_message;

   under_test.handleRequest(request, response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
   EXPECT_THROW(response.get("Retry-After"), Poco::NotFoundException);
   EXPECT_THAT(response.out_stream.str(), testing::HasSubstr("Database not initialized yet"));
}
