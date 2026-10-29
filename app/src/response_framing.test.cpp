#include <array>
#include <memory>
#include <stdexcept>
#include <string>

#include <Poco/Exception.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/StreamCopier.h>
#include <gtest/gtest.h>

#include <rhydb/config/runtime_config.h>

#include "error_request_handler.h"
#include "response_body_stream.h"

// These tests run a real Poco server, because what they check is how a response looks on the wire.

namespace {

const size_t BUFFER_LIMIT = 64;
const std::string PART = std::string(BUFFER_LIMIT, 'x');

/// Writes `parts_before_failure` parts of the body, then fails if `fail` is set.
class BodyWritingHandler : public Poco::Net::HTTPRequestHandler {
   size_t parts;
   bool fail;

  public:
   BodyWritingHandler(size_t parts, bool fail)
       : parts(parts),
         fail(fail) {}

   void handleRequest(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response
   ) override {
      rhydb_app::ResponseBodyStream body{request, response, BUFFER_LIMIT};
      for (size_t i = 0; i < parts; ++i) {
         body << PART;
      }
      if (fail) {
         throw std::runtime_error("failed while writing the body");
      }
      body.finish();
   }
};

class HandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
   const rhydb::config::RuntimeConfig& runtime_config;

  public:
   explicit HandlerFactory(const rhydb::config::RuntimeConfig& runtime_config)
       : runtime_config(runtime_config) {}

   Poco::Net::HTTPRequestHandler* createRequestHandler(
      const Poco::Net::HTTPServerRequest& request
   ) override {
      // URI: /<parts>/<ok|fail>
      const auto separator = request.getURI().find('/', 1);
      const auto parts = std::stoul(request.getURI().substr(1, separator - 1));
      const bool fail = request.getURI().substr(separator + 1) == "fail";
      return new rhydb_app::ErrorRequestHandler(
         std::make_unique<BodyWritingHandler>(parts, fail), runtime_config
      );
   }
};

class ResponseFraming : public ::testing::Test {
  protected:
   rhydb::config::RuntimeConfig runtime_config = rhydb::config::RuntimeConfig::withDefaults();
   Poco::Net::ServerSocket socket{0};
   // Unlike the rhydb server (see api.cpp), this one keeps connections alive (Poco's default): a
   // connection can only be reused if the body before was delimited correctly.
   Poco::Net::HTTPServer server{
      new HandlerFactory(runtime_config),
      socket,
      new Poco::Net::HTTPServerParams
   };

   void SetUp() override { server.start(); }

   void TearDown() override { server.stopAll(true); }

   struct Response {
      Poco::Net::HTTPResponse header;
      std::string body;
   };

   static Response get(const std::string& uri, Poco::Net::HTTPClientSession& session) {
      session.setKeepAlive(true);
      Poco::Net::HTTPRequest request{
         Poco::Net::HTTPRequest::HTTP_GET, uri, Poco::Net::HTTPMessage::HTTP_1_1
      };
      session.sendRequest(request);
      Response response;
      std::istream& body = session.receiveResponse(response.header);
      Poco::StreamCopier::copyToString(body, response.body);
      return response;
   }
};

}  // namespace

TEST_F(ResponseFraming, sendsABodyWithinTheLimitWithContentLengthSoTheConnectionCanBeReused) {
   Poco::Net::HTTPClientSession session{"127.0.0.1", socket.address().port()};

   const auto first = get("/1/ok", session);
   const auto second = get("/1/ok", session);

   EXPECT_EQ(first.header.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
   EXPECT_EQ(first.header.getContentLength64(), BUFFER_LIMIT);
   EXPECT_TRUE(first.header.getKeepAlive());
   EXPECT_EQ(first.body, PART);
   EXPECT_EQ(second.body, PART);
}

TEST_F(ResponseFraming, sendsAnErrorStatusForAFailureWithinTheLimit) {
   Poco::Net::HTTPClientSession session{"127.0.0.1", socket.address().port()};

   const auto response = get("/1/fail", session);

   EXPECT_EQ(response.header.getStatus(), Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
   EXPECT_TRUE(response.header.hasContentLength());
   EXPECT_TRUE(response.header.getKeepAlive());
}

TEST_F(ResponseFraming, streamsABodyOverTheLimitInChunks) {
   Poco::Net::HTTPClientSession session{"127.0.0.1", socket.address().port()};

   const auto response = get("/100/ok", session);

   EXPECT_EQ(response.header.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
   EXPECT_TRUE(response.header.getChunkedTransferEncoding());
   EXPECT_EQ(response.body.size(), 100 * BUFFER_LIMIT);
}

// Poco's own HTTPClientSession takes the end of the connection as the end of a chunked body, so
// this looks at the raw bytes. Strict clients (e.g. curl) report the missing terminating chunk as
// error.
TEST_F(ResponseFraming, leavesAStreamedBodyWithoutTerminatingChunkWhenItFails) {
   Poco::Net::StreamSocket client{Poco::Net::SocketAddress{"127.0.0.1", socket.address().port()}};
   const std::string request = "GET /100/fail HTTP/1.1\r\nHost: localhost\r\n\r\n";
   client.sendBytes(request.data(), static_cast<int>(request.size()));

   std::string raw_response;
   std::array<char, 4096> buffer{};
   int received = 0;
   while ((received = client.receiveBytes(buffer.data(), buffer.size())) > 0) {
      raw_response.append(buffer.data(), static_cast<size_t>(received));
   }

   EXPECT_TRUE(raw_response.starts_with("HTTP/1.1 200 OK"));
   EXPECT_TRUE(raw_response.contains("Transfer-Encoding: chunked"));
   EXPECT_FALSE(raw_response.ends_with("0\r\n\r\n"));
}
