#pragma once

#include <cstddef>
#include <ostream>
#include <streambuf>
#include <string>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

namespace rhydb_app {

/// The body of a response whose size is not known up front, e.g. a query result.
///
/// Collects the body in memory up to `buffer_limit` bytes. A body that stays within the limit is
/// sent with a `Content-Length` by `finish()`. So until the limit is exceeded, nothing is sent and
/// a failure can still be answered with an error status, and the connection can be kept alive.
///
/// A body that exceeds the limit is streamed from then on with bounded memory: in chunked transfer
/// coding to HTTP/1.1 clients, delimited by closing the connection for HTTP/1.0 clients. Once that
/// happened, a failure can only be signalled by aborting the connection (see ErrorRequestHandler),
/// which clients see as a chunked body without its terminating chunk.
///
/// Flushes do not reach the network: the body is sent in blocks of Poco's stream buffer size.
class ResponseBodyStream : public std::ostream {
   class Buffer : public std::streambuf {
     public:
      Poco::Net::HTTPServerRequest& request;
      Poco::Net::HTTPServerResponse& response;
      size_t buffer_limit;
      std::string buffered_body;
      std::ostream* network_stream = nullptr;

      Buffer(
         Poco::Net::HTTPServerRequest& request,
         Poco::Net::HTTPServerResponse& response,
         size_t buffer_limit
      );

      void startStreaming();

     protected:
      std::streamsize xsputn(const char* data, std::streamsize count) override;
      int_type overflow(int_type character) override;
      int sync() override;
   };

   Buffer buffer;

  public:
   static constexpr size_t DEFAULT_BUFFER_LIMIT = size_t{16} * 1024 * 1024;

   ResponseBodyStream(
      Poco::Net::HTTPServerRequest& request,
      Poco::Net::HTTPServerResponse& response,
      size_t buffer_limit = DEFAULT_BUFFER_LIMIT
   );

   /// Sends the buffered body, or the rest of the streamed body. Call only after the whole body
   /// was written successfully.
   void finish();

   [[nodiscard]] bool isStreaming() const;
};

}  // namespace rhydb_app
