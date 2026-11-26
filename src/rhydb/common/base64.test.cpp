#include "rhydb/common/base64.h"

#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <simdutf.h>

TEST(Base64, givenInvalidCharacterReturnsSensibleError) {
   // '!' is not a valid base64 character
   // simdutf reports INVALID_BASE64_CHARACTER which we turn into an error message
   const std::string_view invalid = "!!!!";

   const auto result = rhydb::decodeBase64(invalid);
   ASSERT_FALSE(result.has_value());
   EXPECT_EQ(result.error(), "the encoded string contained an invalid base64 character");
}

TEST(Base64, givenInvalidLengthReturnsSensibleError) {
   // the length must be a multiple of two
   const std::string_view invalid = "aaaAA";

   const auto result = rhydb::decodeBase64(invalid);
   ASSERT_FALSE(result.has_value());
   EXPECT_EQ(result.error(), "invalid padding of base64 input");
}

TEST(Base64, encodesWithPadding) {
   EXPECT_EQ(rhydb::encodeBase64(""), "");
   EXPECT_EQ(rhydb::encodeBase64("f"), "Zg==");
   EXPECT_EQ(rhydb::encodeBase64("fo"), "Zm8=");
   EXPECT_EQ(rhydb::encodeBase64("foo"), "Zm9v");
}

TEST(Base64, encodingRoundTripsArbitraryBytes) {
   const std::string bytes{"\x00\xff\x10 binary\n\x80", 12};
   const auto decoded = rhydb::decodeBase64(rhydb::encodeBase64(bytes));
   ASSERT_TRUE(decoded.has_value());
   EXPECT_EQ(decoded.value(), bytes);
}
