#include "rhydb/config/cors_policy.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "config/config_exception.h"

using rhydb::config::ConfigException;
using rhydb::config::CorsPolicy;
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

TEST(CorsPolicy, matchingIsCaseSensitive) {
   const auto policy = CorsPolicy::parse("https://example.org");
   ASSERT_FALSE(policy.isAllowed("https://EXAMPLE.org"));
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
