#include "rhydb/config/append_config.h"

#include "config/config_interface.h"
#include "config/source/yaml_file.h"

using rhydb::config::ConfigKeyPath;
using rhydb::config::YamlFile;

namespace {
ConfigKeyPath appendConfigOptionKey() {
   return YamlFile::stringToConfigKeyPath("appendConfig");
}
ConfigKeyPath dataDirectoryOptionKey() {
   return YamlFile::stringToConfigKeyPath("dataDirectory");
}
ConfigKeyPath appendFileOptionKey() {
   return YamlFile::stringToConfigKeyPath("appendFile");
}
ConfigKeyPath dataSourceOptionKey() {
   return YamlFile::stringToConfigKeyPath("dataSource");
}
}  // namespace

namespace rhydb::config {

void AppendConfig::validate() const {}

AppendConfig AppendConfig::withDefaults() {
   AppendConfig result;
   result.overwriteFrom(getConfigSpecification().getConfigSourceFromDefaults());
   return result;
}

ConfigSpecification AppendConfig::getConfigSpecification() {
   return ConfigSpecification{
      .program_name = "rhydb append",
      .attribute_specifications{
         ConfigAttributeSpecification::createWithDefault(
            dataDirectoryOptionKey(),
            ConfigValue::fromPath("."),
            "The path to a data directory that contains rhydb outputs. This may be "
            "used for input (see `rhydb api --help`) and will be used for the output of the new "
            "state"
         ),
         ConfigAttributeSpecification::createWithoutDefault(
            appendFileOptionKey(),
            ConfigValueType::PATH,
            "The path to an ndjson file that contains the data that should be appended to "
            "the database. If no file is given, the data is expected on stdin instead."
         ),
         ConfigAttributeSpecification::createWithoutDefault(
            dataSourceOptionKey(),
            ConfigValueType::PATH,
            "A directory that contains a valid rhydb state. If this is not given, the most recent "
            "database state from the data directory is taken instead."
         ),
      },
   };
}

void AppendConfig::overwriteFrom(const VerifiedConfigAttributes& config_source) {
   if (auto var = config_source.getPath(dataDirectoryOptionKey())) {
      data_directory = var.value();
   }
   if (auto var = config_source.getPath(appendFileOptionKey())) {
      append_file = var.value();
   }
   if (auto var = config_source.getPath(dataSourceOptionKey())) {
      data_source = var.value();
   }
}

std::vector<std::filesystem::path> AppendConfig::getConfigFilePaths(
   const VerifiedCommandLineArguments& cmd_source,
   const VerifiedConfigAttributes& env_source
) {
   std::vector<std::filesystem::path> result;
   auto append_config = getConfigFilePath(appendConfigOptionKey(), cmd_source, env_source);
   if (append_config.has_value()) {
      result.emplace_back(append_config.value());
   }
   return result;
}

}  // namespace rhydb::config

[[maybe_unused]] auto fmt::formatter<rhydb::config::AppendConfig>::format(
   const rhydb::config::AppendConfig& append_config,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   const nlohmann::json json = append_config;
   return fmt::format_to(ctx.out(), "{}", json.dump());
}
