#pragma once

#include <sstream>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/SocketAddress.h"

namespace rhydb_app::test {

class MockResponse : public Poco::Net::HTTPServerResponse {
  public:
   std::stringstream out_stream;
   /// Whether the status line and headers went out, as with Poco's own response
   bool was_sent = false;

   void sendContinue() override;

   std::ostream& send() override;

   void sendFile(const std::string& path, const std::string& mediaType) override;

   void sendBuffer(const void* pBuffer, std::size_t length) override;

   void redirect(const std::string& uri, HTTPStatus status) override;

   void requireAuthentication(const std::string& realm) override;

   bool sent() const override;
};

class MockRequest : public Poco::Net::HTTPServerRequest {
  public:
   std::stringstream in_stream;
   Poco::Net::SocketAddress address;
   Poco::Net::HTTPServerParams* params;
   MockResponse& mockResponse;

   explicit MockRequest(MockResponse& mockResponse);

   ~MockRequest() override = default;

   std::istream& stream() override;

   const Poco::Net::SocketAddress& clientAddress() const override;

   const Poco::Net::SocketAddress& serverAddress() const override;

   const Poco::Net::HTTPServerParams& serverParams() const override;

   Poco::Net::HTTPServerResponse& response() const override;

   bool secure() const override;
};

}  // namespace rhydb_app::test
