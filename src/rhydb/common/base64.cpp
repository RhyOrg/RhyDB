#include "rhydb/common/base64.h"

#include <expected>
#include <string>
#include <string_view>

#include <simdutf.h>

namespace rhydb {

std::expected<std::string, std::string> decodeBase64(std::string_view encoded) {
   const size_t max_size =
      simdutf::maximal_binary_length_from_base64(encoded.data(), encoded.size());
   std::string result(max_size, '\0');
   const auto decode_result =
      simdutf::base64_to_binary(encoded.data(), encoded.size(), result.data());
   if (decode_result.error != simdutf::error_code::SUCCESS) {
      switch (decode_result.error) {
         case simdutf::SUCCESS:
            break;
         case simdutf::INVALID_BASE64_CHARACTER:
            return std::unexpected{"the encoded string contained an invalid base64 character"};
         case simdutf::BASE64_INPUT_REMAINDER:
            return std::unexpected{"invalid padding of base64 input"};
         case simdutf::BASE64_EXTRA_BITS:
            return std::unexpected{"the base64 input terminates with non-zero padding bits"};
         case simdutf::OUTPUT_BUFFER_TOO_SMALL:
            return std::unexpected{"the provided buffer is too small, please notify developers"};
         case simdutf::HEADER_BITS:
         case simdutf::OVERLONG:
         case simdutf::SURROGATE:
         case simdutf::TOO_SHORT:
         case simdutf::TOO_LONG:
         case simdutf::TOO_LARGE:
         case simdutf::OTHER:
            return std::unexpected{"an unexpected error occurred when decoding base64 string"};
      }
   }
   result.resize(decode_result.count);
   return result;
}

std::string encodeBase64(std::string_view data) {
   std::string result(simdutf::base64_length_from_binary(data.size()), '\0');
   const size_t written = simdutf::binary_to_base64(data.data(), data.size(), result.data());
   result.resize(written);
   return result;
}

}  // namespace rhydb
