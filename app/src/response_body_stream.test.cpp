#include "response_body_stream.h"

#include <string>

#include <Poco/Net/HTTPMessage.h>
#include <gtest/gtest.h>

#include "manual_poco_mocks.test.h"

namespace {

const size_t BUFFER_LIMIT = 16;

}  // namespace

TEST(ResponseBodyStream, sendsABodyWithinTheLimitWithContentLength) {
   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);
   request.setVersion(Poco::Net::HTTPMessage::HTTP_1_1);

   rhydb_app::ResponseBodyStream body{request, response, BUFFER_LIMIT};
   body << "{\"a\":1}\n" << std::flush;
   body << "{\"a\":2}\n" << std::flush;

   EXPECT_FALSE(response.sent());

   body.finish();

   EXPECT_FALSE(body.isStreaming());
   EXPECT_TRUE(response.sent());
   EXPECT_EQ(response.getContentLength64(), 16);
   EXPECT_FALSE(response.getChunkedTransferEncoding());
   EXPECT_EQ(response.out_stream.str(), "{\"a\":1}\n{\"a\":2}\n");
}

TEST(ResponseBodyStream, sendsNothingWhenNotFinished) {
   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);

   {
      rhydb_app::ResponseBodyStream body{request, response, BUFFER_LIMIT};
      body << "partial";
   }

   EXPECT_FALSE(response.sent());
   EXPECT_EQ(response.out_stream.str(), "");
}

TEST(ResponseBodyStream, streamsABodyOverTheLimitChunkedToHttp11Clients) {
   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);
   request.setVersion(Poco::Net::HTTPMessage::HTTP_1_1);

   rhydb_app::ResponseBodyStream body{request, response, BUFFER_LIMIT};
   body << "0123456789";
   EXPECT_FALSE(body.isStreaming());
   body << "0123456789";
   EXPECT_TRUE(body.isStreaming());
   body << "tail";
   body.finish();

   EXPECT_TRUE(response.getChunkedTransferEncoding());
   EXPECT_FALSE(response.hasContentLength());
   EXPECT_EQ(response.out_stream.str(), "01234567890123456789tail");
}

TEST(ResponseBodyStream, streamsABodyOverTheLimitUntilConnectionCloseToHttp10Clients) {
   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);
   request.setVersion(Poco::Net::HTTPMessage::HTTP_1_0);

   rhydb_app::ResponseBodyStream body{request, response, BUFFER_LIMIT};
   body << std::string(BUFFER_LIMIT + 1, 'x');
   body.finish();

   EXPECT_TRUE(body.isStreaming());
   EXPECT_FALSE(response.getChunkedTransferEncoding());
   EXPECT_FALSE(response.getKeepAlive());
   EXPECT_EQ(response.out_stream.str(), std::string(BUFFER_LIMIT + 1, 'x'));
}
