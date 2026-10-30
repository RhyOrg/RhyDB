#include "cors_handler.h"

#include <Poco/Net/HTTPResponse.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <rhydb/config/cors_policy.h>

#include "manual_poco_mocks.test.h"

using rhydb::config::CorsPolicy;
using rhydb_app::CorsHandler;

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

struct Fixture {
   std::unique_ptr<MockRequestHandler> wrapped = std::make_unique<MockRequestHandler>();
   MockRequestHandler* wrapped_ptr = wrapped.get();
   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request{response};
};

}  // namespace

TEST(CorsHandler, disabled_passesThroughWithoutHeaders) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest);
   auto under_test = CorsHandler(std::move(fixture.wrapped), std::nullopt);

   fixture.request.setMethod("GET");
   fixture.request.set("Origin", "https://example.org");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_FALSE(fixture.response.has("Access-Control-Allow-Origin"));
}

TEST(CorsHandler, noOriginHeader_passesThroughWithoutHeaders) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest);
   auto under_test = CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("*"));

   fixture.request.setMethod("GET");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_FALSE(fixture.response.has("Access-Control-Allow-Origin"));
}

TEST(CorsHandler, wildcard_setsStarAndExposeHeaders) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest);
   auto under_test = CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("*"));

   fixture.request.setMethod("GET");
   fixture.request.set("Origin", "https://anything.example");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_EQ(fixture.response.get("Access-Control-Allow-Origin"), "*");
   ASSERT_EQ(
      fixture.response.get("Access-Control-Expose-Headers"),
      "X-Request-Id, data-version, result-ordering, Retry-After"
   );
   // Wildcard mode does not vary by origin.
   ASSERT_FALSE(fixture.response.has("Vary"));
}

TEST(CorsHandler, allowedOriginInList_echoesOriginAndSetsVary) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest);
   auto under_test =
      CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("https://example.org"));

   fixture.request.setMethod("GET");
   fixture.request.set("Origin", "https://example.org");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_EQ(fixture.response.get("Access-Control-Allow-Origin"), "https://example.org");
   ASSERT_EQ(fixture.response.get("Vary"), "Origin");
}

TEST(CorsHandler, disallowedOrigin_passesThroughWithVaryButNoAllowOrigin) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest);
   auto under_test =
      CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("https://example.org"));

   fixture.request.setMethod("GET");
   fixture.request.set("Origin", "https://evil.example");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_FALSE(fixture.response.has("Access-Control-Allow-Origin"));
   // Still varies by origin so caches do not serve this to an allowed origin.
   ASSERT_EQ(fixture.response.get("Vary"), "Origin");
}

TEST(CorsHandler, preflightFromAllowedOrigin_returns204WithHeadersAndDoesNotDelegate) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest).Times(0);
   auto under_test = CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("*"));

   fixture.request.setMethod("OPTIONS");
   fixture.request.set("Origin", "https://anything.example");
   fixture.request.set("Access-Control-Request-Method", "POST");
   fixture.request.set("Access-Control-Request-Headers", "content-type, x-request-id");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_EQ(fixture.response.getStatus(), Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
   ASSERT_EQ(fixture.response.get("Access-Control-Allow-Origin"), "*");
   ASSERT_EQ(fixture.response.get("Access-Control-Allow-Methods"), "GET, POST, OPTIONS");
   ASSERT_EQ(fixture.response.get("Access-Control-Allow-Headers"), "content-type, x-request-id");
   ASSERT_EQ(fixture.response.get("Access-Control-Max-Age"), "7200");
}

TEST(CorsHandler, preflightWithoutRequestHeaders_omitsAllowHeaders) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest).Times(0);
   auto under_test = CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("*"));

   fixture.request.setMethod("OPTIONS");
   fixture.request.set("Origin", "https://anything.example");
   fixture.request.set("Access-Control-Request-Method", "POST");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_EQ(fixture.response.getStatus(), Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
   ASSERT_FALSE(fixture.response.has("Access-Control-Allow-Headers"));
}

TEST(CorsHandler, preflightFromDisallowedOrigin_returns403AndDoesNotDelegate) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest).Times(0);
   auto under_test =
      CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("https://example.org"));

   fixture.request.setMethod("OPTIONS");
   fixture.request.set("Origin", "https://evil.example");
   fixture.request.set("Access-Control-Request-Method", "POST");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_EQ(fixture.response.getStatus(), Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
   ASSERT_FALSE(fixture.response.has("Access-Control-Allow-Origin"));
}

// An OPTIONS request without Access-Control-Request-Method is not a preflight; it is delegated
// so the routed handler returns its normal 404/405.
TEST(CorsHandler, optionsWithoutPreflightHeader_isDelegated) {
   Fixture fixture;
   EXPECT_CALL(*fixture.wrapped_ptr, handleRequest);
   auto under_test = CorsHandler(std::move(fixture.wrapped), CorsPolicy::parse("*"));

   fixture.request.setMethod("OPTIONS");
   fixture.request.set("Origin", "https://anything.example");
   under_test.handleRequest(fixture.request, fixture.response);

   ASSERT_EQ(fixture.response.get("Access-Control-Allow-Origin"), "*");
}
