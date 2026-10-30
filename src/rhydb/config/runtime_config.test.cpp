#include "rhydb/config/runtime_config.h"

#include <gtest/gtest.h>

#include "config/config_exception.h"
#include "config/source/yaml_file.h"

using rhydb::config::RuntimeConfig;
using rhydb::config::YamlFile;

TEST(RuntimeConfig, shouldReadConfig) {
   auto runtime_config = RuntimeConfig::withDefaults();

   auto source = YamlFile::readFile("./testBaseData/test_runtime_config.yaml")
                    .verify(RuntimeConfig::getConfigSpecification());

   runtime_config.overwriteFrom(source);

   ASSERT_EQ(runtime_config.api_options.address, "127.0.0.1");
   ASSERT_EQ(runtime_config.api_options.port, 1234);
   ASSERT_EQ(runtime_config.data_directory, "test/directory");
   ASSERT_TRUE(runtime_config.api_options.allow_admin_endpoint);
   ASSERT_EQ(
      runtime_config.api_options.cors_allowed_origins, "https://example.org,https://other.org:8443"
   );
}

// The write-enabled admin endpoint must stay off unless it is explicitly switched on, so that an
// existing read-only instance stays read-only after an upgrade.
TEST(RuntimeConfig, adminEndpointIsDisabledByDefault) {
   ASSERT_FALSE(RuntimeConfig::withDefaults().api_options.allow_admin_endpoint);
}

// CORS is opt-in: without configuration no CORS headers must be sent.
TEST(RuntimeConfig, corsIsDisabledByDefault) {
   ASSERT_FALSE(RuntimeConfig::withDefaults().api_options.cors_allowed_origins.has_value());
}

// A malformed value must fail at startup, not on the first request.
TEST(RuntimeConfig, validateRejectsMalformedCorsOrigins) {
   auto runtime_config = RuntimeConfig::withDefaults();
   runtime_config.api_options.cors_allowed_origins = "not-an-origin";
   EXPECT_THROW(runtime_config.validate(), rhydb::config::ConfigException);
}
