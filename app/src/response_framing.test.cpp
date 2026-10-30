#include <array>
#include <memory>
#include <stdexcept>
#include <string>

#include <Poco/Exception.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include <gtest/gtest.h>

#include <rhydb/config/runtime_config.h>

#include "error_request_handler.h"

// These tests run a real Poco server, because what they check is how a response looks on the wire.

namespace {

const std::string PART = std::string(64, 'x');

/// Starts a chunked response, then fails.
class FailingAfterStartHandler : public Poco::Net::HTTPRequestHandler {
  public:
   void handleRequest(
      Poco::Net::HTTPServerRequest& /*request*/,
      Poco::Net::HTTPServerResponse& response
   ) override {
      response.setChunkedTransferEncoding(true);
      std::ostream& body = response.send();
      body << PART;
      body.flush();
      throw std::runtime_error("failed while writing the body");
   }
};

class HandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
   const rhydb::config::RuntimeConfig& runtime_config;

  public:
   explicit HandlerFactory(const rhydb::config::RuntimeConfig& runtime_config)
       : runtime_config(runtime_config) {}

   Poco::Net::HTTPRequestHandler* createRequestHandler(
      const Poco::Net::HTTPServerRequest& /*request*/
   ) override {
      return new rhydb_app::ErrorRequestHandler(
         std::make_unique<FailingAfterStartHandler>(), runtime_config
      );
   }
};

class ResponseFraming : public ::testing::Test {
  protected:
   rhydb::config::RuntimeConfig runtime_config = rhydb::config::RuntimeConfig::withDefaults();
   Poco::Net::ServerSocket socket{0};
   // Unlike the rhydb server (see api.cpp), this one keeps connections alive (Poco's default), so
   // that a connection that is not aborted stays open.
   Poco::Net::HTTPServer server{
      new HandlerFactory(runtime_config),
      socket,
      new Poco::Net::HTTPServerParams
   };

   void SetUp() override { server.start(); }

   void TearDown() override { server.stopAll(true); }
};

}  // namespace

// Covers ErrorRequestHandler aborting the connection, which the unit tests with mocked requests
// cannot reach. Without the abort, Poco would write the terminating chunk and keep the connection
// open. Poco's own HTTPClientSession takes the end of the connection as the end of a chunked body,
// so this looks at the raw bytes. Strict clients (e.g. curl) report the missing terminating chunk
// as error.
TEST_F(ResponseFraming, abortsTheConnectionWithoutTerminatingChunkWhenAStreamedBodyFails) {
   Poco::Net::StreamSocket client{Poco::Net::SocketAddress{"127.0.0.1", socket.address().port()}};
   // Well below the server's keep-alive timeout, so a connection kept open fails the test
   client.setReceiveTimeout(Poco::Timespan{5, 0});
   const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
   client.sendBytes(request.data(), static_cast<int>(request.size()));

   std::string raw_response;
   std::array<char, 4096> buffer{};
   int received = 0;
   try {
      while ((received = client.receiveBytes(buffer.data(), buffer.size())) > 0) {
         raw_response.append(buffer.data(), static_cast<size_t>(received));
      }
   } catch (const Poco::TimeoutException&) {
      FAIL() << "the server kept the connection open";
   }

   EXPECT_TRUE(raw_response.starts_with("HTTP/1.1 200 OK"));
   EXPECT_TRUE(raw_response.contains("Transfer-Encoding: chunked"));
   EXPECT_FALSE(raw_response.ends_with("0\r\n\r\n"));
}
