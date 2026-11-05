#include "cors_policy.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "config/config_exception.h"

using rhydb::config::ConfigException;
using rhydb_app::CorsPolicy;
using ::testing::HasSubstr;

TEST(CorsPolicy, wildcardAllowsAnyOrigin) {
   const auto policy = CorsPolicy::parse("*");
   ASSERT_TRUE(policy.allowsAnyOrigin());
   ASSERT_TRUE(policy.isAllowed("https://anything.example"));
}

TEST(CorsPolicy, singleOriginIsAllowedExactly) {
   const auto policy = CorsPolicy::parse("https://example.org");
   ASSERT_FALSE(policy.allowsAnyOrigin());
   ASSERT_TRUE(policy.isAllowed("https://example.org"));
   ASSERT_FALSE(policy.isAllowed("https://other.org"));
   // Matching is exact: a trailing slash or different port does not match.
   ASSERT_FALSE(policy.isAllowed("https://example.org/"));
   ASSERT_FALSE(policy.isAllowed("https://example.org:8443"));
}

TEST(CorsPolicy, listOfOriginsWithWhitespaceAndPorts) {
   const auto policy =
      CorsPolicy::parse("https://a.example , https://b.example:8443 ,http://localhost:3000");
   ASSERT_FALSE(policy.allowsAnyOrigin());
   ASSERT_TRUE(policy.isAllowed("https://a.example"));
   ASSERT_TRUE(policy.isAllowed("https://b.example:8443"));
   ASSERT_TRUE(policy.isAllowed("http://localhost:3000"));
   ASSERT_FALSE(policy.isAllowed("https://c.example"));
}

TEST(CorsPolicy, allowsBracketedIpv6Origin) {
   const auto policy = CorsPolicy::parse("http://[::1]:3000,https://[2001:db8::1]");
   ASSERT_TRUE(policy.isAllowed("http://[::1]:3000"));
   ASSERT_TRUE(policy.isAllowed("https://[2001:db8::1]"));
   ASSERT_FALSE(policy.isAllowed("http://[::1]"));
}

TEST(CorsPolicy, matchingIsCaseSensitive) {
   const auto policy = CorsPolicy::parse("https://example.org");
   ASSERT_FALSE(policy.isAllowed("https://EXAMPLE.org"));
}

// Browsers send the scheme and host lowercased, so a configured entry with uppercase letters is
// normalized to match rather than being silently ineffective.
TEST(CorsPolicy, normalizesSchemeAndHostToLowercase) {
   const auto policy = CorsPolicy::parse("HTTPS://Example.ORG");
   ASSERT_TRUE(policy.isAllowed("https://example.org"));
}

// Browsers omit a scheme's default port from the serialized origin, so a configured default port is
// stripped to match (http -> 80, https -> 443).
TEST(CorsPolicy, normalizesDefaultPortAway) {
   const auto policy = CorsPolicy::parse("https://example.org:443,http://other.example:80");
   ASSERT_TRUE(policy.isAllowed("https://example.org"));
   ASSERT_TRUE(policy.isAllowed("http://other.example"));
   // The default port is not part of the normalized origin, so the explicit form does not match.
   ASSERT_FALSE(policy.isAllowed("https://example.org:443"));
}

// A non-default port is kept during normalization.
TEST(CorsPolicy, keepsNonDefaultPort) {
   const auto policy = CorsPolicy::parse("https://example.org:8443");
   ASSERT_TRUE(policy.isAllowed("https://example.org:8443"));
   ASSERT_FALSE(policy.isAllowed("https://example.org"));
}

TEST(CorsPolicy, rejectsEmptyEntry) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://a.example,,https://b.example"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("empty origins"))
   );
}

TEST(CorsPolicy, rejectsEmptyValue) {
   EXPECT_THAT(
      []() { CorsPolicy::parse(""); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("empty origins"))
   );
}

TEST(CorsPolicy, rejectsWildcardMixedWithOrigins) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("*,https://a.example"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("cannot be combined"))
   );
}

TEST(CorsPolicy, rejectsOriginWithTrailingSlash) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://a.example/"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
}

TEST(CorsPolicy, rejectsOriginWithPath) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://a.example/foo"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
}

TEST(CorsPolicy, rejectsOriginWithoutScheme) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("a.example"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
}

TEST(CorsPolicy, rejectsPortOutOfRange) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://example.org:65536"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://example.org:99999999999999999999"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
}

TEST(CorsPolicy, allowsHighestValidPort) {
   const auto policy = CorsPolicy::parse("https://example.org:65535");
   ASSERT_TRUE(policy.isAllowed("https://example.org:65535"));
}

TEST(CorsPolicy, rejectsMalformedIpv6Literal) {
   EXPECT_THAT(
      []() { CorsPolicy::parse("http://[:]"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
   EXPECT_THAT(
      []() { CorsPolicy::parse("http://[12345::6789::1]"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
}

TEST(CorsPolicy, rejectsExplicitZeroPort) {
   // Poco maps an explicit `:0` to the same specified port as an absent one, so it would normalize
   // away to a different origin than configured. Port 0 is not a real origin, so it is rejected.
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://example.org:0"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
   EXPECT_THAT(
      []() { CorsPolicy::parse("http://[::1]:0"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
   EXPECT_THAT(
      []() { CorsPolicy::parse("https://example.org:00"); },
      ::testing::ThrowsMessage<ConfigException>(HasSubstr("not a valid origin"))
   );
}
