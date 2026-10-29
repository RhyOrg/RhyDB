#include "response_body_stream.h"

#include <Poco/Net/HTTPMessage.h>

namespace rhydb_app {

ResponseBodyStream::Buffer::Buffer(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response,
   size_t buffer_limit
)
    : request(request),
      response(response),
      buffer_limit(buffer_limit) {}

void ResponseBodyStream::Buffer::startStreaming() {
   if (request.getVersion() == Poco::Net::HTTPMessage::HTTP_1_1) {
      response.setChunkedTransferEncoding(true);
   } else {
      // HTTP/1.0 has no chunked transfer coding, so only closing the connection ends the body
      response.setKeepAlive(false);
   }
   network_stream = &response.send();
   network_stream->write(buffered_body.data(), static_cast<std::streamsize>(buffered_body.size()));
   // Release the memory, not just the contents
   std::string{}.swap(buffered_body);
}

std::streamsize ResponseBodyStream::Buffer::xsputn(const char* data, std::streamsize count) {
   if (network_stream == nullptr) {
      if (buffered_body.size() + static_cast<size_t>(count) <= buffer_limit) {
         buffered_body.append(data, static_cast<size_t>(count));
         return count;
      }
      startStreaming();
   }
   network_stream->write(data, count);
   // A failed write means the client is gone, which the writer sees as the badbit of our stream
   return *network_stream ? count : 0;
}

ResponseBodyStream::Buffer::int_type ResponseBodyStream::Buffer::overflow(int_type character) {
   if (traits_type::eq_int_type(character, traits_type::eof())) {
      return traits_type::not_eof(character);
   }
   const char as_char = traits_type::to_char_type(character);
   return xsputn(&as_char, 1) == 1 ? character : traits_type::eof();
}

int ResponseBodyStream::Buffer::sync() {
   // Writers flush after small pieces (e.g. every NDJSON line). Passing that on would turn each
   // piece into its own socket write.
   return 0;
}

ResponseBodyStream::ResponseBodyStream(
   Poco::Net::HTTPServerRequest& request,
   Poco::Net::HTTPServerResponse& response,
   size_t buffer_limit
)
    : std::ostream(nullptr),
      buffer(request, response, buffer_limit) {
   rdbuf(&buffer);
}

void ResponseBodyStream::finish() {
   if (buffer.network_stream == nullptr) {
      buffer.response.sendBuffer(buffer.buffered_body.data(), buffer.buffered_body.size());
   } else {
      buffer.network_stream->flush();
   }
}

bool ResponseBodyStream::isStreaming() const {
   return buffer.network_stream != nullptr;
}

}  // namespace rhydb_app
