#include "rhydb/config/runtime_config.h"

#include <string>

#include <spdlog/spdlog.h>
#include <boost/algorithm/string/join.hpp>
#include <nlohmann/json.hpp>

#include "config/config_interface.h"
#include "config/source/yaml_file.h"
#include "rhydb/common/json_type_definitions.h"
#include "rhydb/config/config_defaults.h"

namespace {
using rhydb::config::ConfigKeyPath;
using rhydb::config::YamlFile;

// Using functions instead of global variables because of
// initialization order issues.

ConfigKeyPath runtimeConfigOptionKey() {
   return YamlFile::stringToConfigKeyPath("runtimeConfig");
}
ConfigKeyPath defaultRuntimeConfigOptionKey() {
   return YamlFile::stringToConfigKeyPath("defaultRuntimeConfig");
}
ConfigKeyPath dataDirectoryOptionKey() {
   return YamlFile::stringToConfigKeyPath("dataDirectory");
}
ConfigKeyPath apiAddressOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.address");
}
ConfigKeyPath apiPortOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.port");
}
ConfigKeyPath apiMaxConnectionsOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.maxQueuedHttpConnections");
}
ConfigKeyPath apiParallelThreadsOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.threadsForHttpConnections");
}
ConfigKeyPath apiEstimatedStartupTimeOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.estimatedStartupTimeInMinutes");
}
ConfigKeyPath softMemoryLimitOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.softMemoryLimit");
}
ConfigKeyPath apiAllowAdminEndpointOptionKey() {
   return YamlFile::stringToConfigKeyPath("api.allowAdminEndpoint");
}
ConfigKeyPath queryMaterializationOptionKey() {
   return YamlFile::stringToConfigKeyPath("query.materializationCutoff");
}

}  // namespace

namespace rhydb::config {

// arrow::acero::SourceNode reslices batches if they are bigger than 32768
const uint32_t DEFAULT_ARROW_BATCH_SIZE = 32767;

ConfigSpecification RuntimeConfig::getConfigSpecification() {
   return {
      .program_name = "rhydb api",
      .attribute_specifications =
         {
            ConfigAttributeSpecification::createWithoutDefault(
               runtimeConfigOptionKey(),
               ConfigValueType::PATH,
               "The path to the config file in YAML format."
            ),
            ConfigAttributeSpecification::createWithoutDefault(
               defaultRuntimeConfigOptionKey(),
               ConfigValueType::PATH,
               "The path to config file in YAML format with default values. \n"
               "This path will often be set by an environment variable, thus \n"
               "providing defaults to a RhyDB instance in a specific environment (e.g. Docker)."
            ),
            ConfigAttributeSpecification::createWithDefault(
               dataDirectoryOptionKey(),
               ConfigValue::fromPath(DEFAULT_OUTPUT_DIRECTORY),
               "The path to the directory with the data files (output from preprocessing)."
            ),
            ConfigAttributeSpecification::createWithDefault(
               apiAddressOptionKey(),
               ConfigValue::fromString("0.0.0.0"),
               "The address on which to listen for incoming HTTP connections. \n"
               "Defaults to 0.0.0.0, which binds to all available network interfaces. \n"
               "Set to 127.0.0.1 to only accept connections from localhost."
            ),
            ConfigAttributeSpecification::createWithDefault(
               apiPortOptionKey(),
               ConfigValue::fromUint16(8081),
               "The port number on which to listen for incoming HTTP connections."
            ),
            ConfigAttributeSpecification::createWithDefault(
               apiMaxConnectionsOptionKey(),
               ConfigValue::fromInt32(256),
               "The maximum number of concurrent connections accepted at any time."
            ),
            ConfigAttributeSpecification::createWithDefault(
               apiParallelThreadsOptionKey(),
               ConfigValue::fromInt32(0),
               "The number of worker threads. If set to 0 it will be set to the number of "
               "processors."
            ),
            ConfigAttributeSpecification::createWithoutDefault(
               apiEstimatedStartupTimeOptionKey(),
               ConfigValueType::UINT32,
               "Estimated time in minutes that the initial loading of the database takes. \n"
               "As long as no database is loaded yet, RhyDB will throw a 503 error. \n"
               "This option allows RhyDB to compute a Retry-After header for the 503 response."
            ),
            ConfigAttributeSpecification::createWithDefault(
               softMemoryLimitOptionKey(),
               ConfigValue::fromUint32(0),
               "A soft-limit on the memory usage. If the rss of the process is higher than \n"
               "this value, malloc_trim is called. \n"
               "Only supported on Linux."
            ),
            ConfigAttributeSpecification::createWithDefault(
               apiAllowAdminEndpointOptionKey(),
               ConfigValue::fromBool(false),
               "Whether to serve the write-enabled 'POST /admin/query' endpoint, which changes \n"
               "the data the server serves (e.g. via 'insertInto'). Each write saves a new data \n"
               "version to the data directory, which must be writable, and is served once it has \n"
               "been picked up from there. Disabled by default; while it is disabled the endpoint "
               "\n"
               "responds with 404 and the instance stays read-only."
            ),
            ConfigAttributeSpecification::createWithDefault(
               queryMaterializationOptionKey(),
               ConfigValue::fromUint32(DEFAULT_ARROW_BATCH_SIZE),
               "If a query results in fewer rows, the query result will be collected \n"
               "in memory before sending it to the client. If it affects more rows, \n"
               "it will be streamed by constructing the result items lazily."
            ),
         },
   };
}

RuntimeConfig RuntimeConfig::withDefaults() {
   RuntimeConfig config;
   config.overwriteFrom(getConfigSpecification().getConfigSourceFromDefaults());
   return config;
}

std::vector<std::filesystem::path> RuntimeConfig::getConfigFilePaths(
   const VerifiedCommandLineArguments& cmd_source,
   const VerifiedConfigAttributes& env_source
) {
   std::vector<std::filesystem::path> result;
   auto default_runtime_config =
      getConfigFilePath(defaultRuntimeConfigOptionKey(), cmd_source, env_source);
   if (default_runtime_config.has_value()) {
      result.emplace_back(default_runtime_config.value());
   }
   auto runtime_config = getConfigFilePath(runtimeConfigOptionKey(), cmd_source, env_source);
   if (runtime_config.has_value()) {
      result.emplace_back(runtime_config.value());
   }
   return result;
}

void RuntimeConfig::overwriteFrom(const VerifiedConfigAttributes& config_source) {
   if (auto var = config_source.getPath(dataDirectoryOptionKey())) {
      data_directory = var.value();
   }
   if (auto var = config_source.getString(apiAddressOptionKey())) {
      api_options.address = var.value();
   }
   if (auto var = config_source.getUint16(apiPortOptionKey())) {
      api_options.port = var.value();
   }
   if (auto var = config_source.getInt32(apiMaxConnectionsOptionKey())) {
      api_options.max_connections = var.value();
   }
   if (auto var = config_source.getInt32(apiParallelThreadsOptionKey())) {
      api_options.parallel_threads = var.value();
   }
   // But estimated_startup_end is a derived value:
   if (auto var = config_source.getUint32(apiEstimatedStartupTimeOptionKey())) {
      const std::chrono::minutes minutes = std::chrono::minutes(var.value());
      api_options.estimated_startup_end = std::chrono::system_clock::now() + minutes;
   }
   if (auto var = config_source.getUint32(softMemoryLimitOptionKey())) {
      api_options.soft_memory_limit = var.value();
   }
   if (auto var = config_source.getBool(apiAllowAdminEndpointOptionKey())) {
      api_options.allow_admin_endpoint = var.value();
   }
   if (auto var = config_source.getUint32(queryMaterializationOptionKey())) {
      query_options.materialization_cutoff = var.value();
   }
}

}  // namespace rhydb::config

namespace nlohmann {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
   rhydb::config::ApiOptions,
   max_connections,
   parallel_threads,
   address,
   port,
   estimated_startup_end,
   allow_admin_endpoint
)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(rhydb::config::QueryOptions, materialization_cutoff)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
   rhydb::config::RuntimeConfig,
   data_directory,
   api_options,
   query_options
)

}  // namespace nlohmann

[[maybe_unused]] auto fmt::formatter<rhydb::config::RuntimeConfig>::format(
   const rhydb::config::RuntimeConfig& runtime_config,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   const nlohmann::json json = runtime_config;
   return fmt::format_to(ctx.out(), "{}", json.dump());
}
