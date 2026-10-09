#include "config/source/environment_variables.h"

#include <cstddef>

#include <spdlog/spdlog.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

constexpr std::string_view ENV_VAR_PREFIX = "RHYDB_";
constexpr std::string_view DEPRECATED_ENV_VAR_PREFIX = "SILO_";

namespace {

std::string toLowerCase(std::string input) {
   std::string result;
   std::ranges::transform(input, std::back_inserter(result), ::tolower);
   return result;
}

}  // namespace

namespace rhydb::config {

EnvironmentVariables EnvironmentVariables::newWithAllowListAndEnv(
   const std::vector<std::string>& allow_list,
   const char* const* envp
) {
   std::vector<std::pair<std::string, std::string>> key_value_pairs;
   for (const char* const* current_envp = envp; *current_envp != nullptr; current_envp++) {
      const char* env = *current_envp;
      for (size_t i = 0; env[i] != 0; i++) {
         if (env[i] == '=') {
            const std::string key{env, i};
            if (key.starts_with(ENV_VAR_PREFIX)) {
               const std::string val{env + i + 1};
               key_value_pairs.emplace_back(key, val);
            } else if (key.starts_with(DEPRECATED_ENV_VAR_PREFIX)) {
               SPDLOG_WARN(
                  "Ignoring environment variable '{}': the '{}' prefix is deprecated and no longer "
                  "read. Environment variables now use the '{}' prefix.",
                  key,
                  DEPRECATED_ENV_VAR_PREFIX,
                  ENV_VAR_PREFIX
               );
            }
            break;
         }
      }
   }
   return EnvironmentVariables{std::move(key_value_pairs), allow_list};
}

[[nodiscard]] std::string EnvironmentVariables::configKeyPathToString(
   const ConfigKeyPath& config_key_path
) {
   std::vector<std::string> result;
   for (const auto& sublevel : config_key_path.getPath()) {
      for (const std::string& current_string : sublevel) {
         std::string current_string_all_uppercase;
         std::ranges::transform(
            current_string,
            std::back_inserter(current_string_all_uppercase),
            [](unsigned char character) { return std::toupper(character); }
         );
         result.push_back(current_string_all_uppercase);
      }
   }
   return fmt::format("{}{}", ENV_VAR_PREFIX, boost::join(result, "_"));
}

AmbiguousConfigKeyPath EnvironmentVariables::stringToConfigKeyPath(
   const std::string& key_path_string
) {
   if (!key_path_string.starts_with(ENV_VAR_PREFIX)) {
      throw ConfigException(
         fmt::format(
            "the provided option '{}' is not a valid environment variable option. It should be "
            "prefixed with '{}'",
            key_path_string,
            ENV_VAR_PREFIX
         )
      );
   }

   // Remove the prefix
   const std::string trimmed = key_path_string.substr(ENV_VAR_PREFIX.size());

   std::vector<std::string> delimited_strings;
   boost::split(delimited_strings, trimmed, boost::is_any_of("_"));

   std::vector<std::string> delimited_lowercase_strings;
   std::ranges::transform(
      delimited_strings, std::back_inserter(delimited_lowercase_strings), toLowerCase
   );

   auto result = AmbiguousConfigKeyPath::tryFrom(std::move(delimited_lowercase_strings));
   if (result == std::nullopt) {
      throw ConfigException(
         fmt::format(
            "the provided option '{}' is not a valid environment variable option", key_path_string
         )
      );
   }
   return result.value();
}

[[nodiscard]] VerifiedConfigAttributes EnvironmentVariables::verify(
   const ConfigSpecification& config_specification
) const {
   std::unordered_map<ConfigKeyPath, ConfigValue> config_values;
   std::vector<std::string> invalid_config_keys;
   for (const auto& [key_string, value_string] : key_value_pairs) {
      auto ambiguous_key = EnvironmentVariables::stringToConfigKeyPath(key_string);
      auto value_specification_opt =
         config_specification.getAttributeSpecificationFromAmbiguousKey(ambiguous_key);
      if (value_specification_opt.has_value()) {
         auto attribute_spec = value_specification_opt.value();
         const ConfigValue value = attribute_spec.parseValueFromString(value_string);
         config_values.emplace(attribute_spec.key, value);
      } else {
         if (std::ranges::find(allow_list, key_string) != allow_list.end()) {
            SPDLOG_INFO(
               "Given env variable '{}' is not a valid key for '{}' but in allow_list.",
               key_string,
               config_specification.program_name
            );
         } else {
            invalid_config_keys.push_back(key_string);
         }
      }
   }

   if (!invalid_config_keys.empty()) {
      const std::string_view keys_or_options =
         (invalid_config_keys.size() >= 2) ? "variables" : "variable";
      throw ConfigException(
         fmt::format(
            "in {}: unknown {} {} for '{}'",
            debugContext(),
            keys_or_options,
            boost::join(invalid_config_keys, ", "),
            config_specification.program_name
         )
      );
   }

   return VerifiedConfigAttributes{config_values};
}

}  // namespace rhydb::config
