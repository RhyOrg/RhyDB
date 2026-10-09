#include "config/verified_config_attributes.h"
#include <spdlog/spdlog.h>

#include "config/source/yaml_file.h"
#include "rhydb/common/panic.h"

namespace {
using rhydb::config::ConfigKeyPath;
using rhydb::config::ConfigValue;
using rhydb::config::ConfigValueType;
using rhydb::config::YamlFile;

template <typename T, ConfigValueType ExpectedType>
std::optional<T> getValue(
   const ConfigKeyPath& config_key_path,
   const std::unordered_map<ConfigKeyPath, ConfigValue>& config_values
) {
   auto value_it = config_values.find(config_key_path);
   if (value_it != config_values.end()) {
      const ConfigValue& value = value_it->second;
      if (value.getValueType() != ExpectedType) {
         RHYDB_PANIC(
            "Called getValue with type {} on a ConfigKeyPath ('{}') that belongs to a value of "
            "another type ({}).",
            configValueTypeToString(ExpectedType),
            YamlFile::configKeyPathToString(config_key_path),
            configValueTypeToString(value.getValueType())
         );
      }
      SPDLOG_TRACE(
         "Using for key `{}` the value {}",
         YamlFile::configKeyPathToString(config_key_path),
         value.toString()
      );
      return std::get<T>(value.value);
   }
   return std::nullopt;
}
}  // namespace

namespace rhydb::config {

std::optional<std::string> VerifiedConfigAttributes::getString(
   const ConfigKeyPath& config_key_path
) const {
   return getValue<std::string, ConfigValueType::STRING>(config_key_path, config_values);
}

std::optional<std::filesystem::path> VerifiedConfigAttributes::getPath(
   const ConfigKeyPath& config_key_path
) const {
   return getValue<std::filesystem::path, ConfigValueType::PATH>(config_key_path, config_values);
}

std::optional<int32_t> VerifiedConfigAttributes::getInt32(
   const ConfigKeyPath& config_key_path
) const {
   return getValue<int32_t, ConfigValueType::INT32>(config_key_path, config_values);
}

std::optional<uint32_t> VerifiedConfigAttributes::getUint32(
   const ConfigKeyPath& config_key_path
) const {
   return getValue<uint32_t, ConfigValueType::UINT32>(config_key_path, config_values);
}

std::optional<uint16_t> VerifiedConfigAttributes::getUint16(
   const ConfigKeyPath& config_key_path
) const {
   return getValue<uint16_t, ConfigValueType::UINT16>(config_key_path, config_values);
}

std::optional<bool> VerifiedConfigAttributes::getBool(const ConfigKeyPath& config_key_path) const {
   auto value_it = config_values.find(config_key_path);
   if (value_it != config_values.end()) {
      const ConfigValue& value = value_it->second;
      if (value.getValueType() != ConfigValueType::BOOL) {
         RHYDB_PANIC(
            "Called getBool on a ConfigKeyPath ('{}') that belongs to a value of another "
            "type ({}).",
            YamlFile::configKeyPathToString(config_key_path),
            configValueTypeToString(value.getValueType())
         );
      }
      SPDLOG_TRACE(
         "Using for key `{}` the value {}",
         YamlFile::configKeyPathToString(config_key_path),
         value.toString()
      );
      return get<bool>(value.value);
   }
   return std::nullopt;
}

std::optional<std::vector<std::string>> VerifiedConfigAttributes::getList(
   const ConfigKeyPath& config_key_path
) const {
   return getValue<std::vector<std::string>, ConfigValueType::LIST>(config_key_path, config_values);
}

VerifiedCommandLineArguments VerifiedCommandLineArguments::askingForHelp() {
   VerifiedCommandLineArguments result;
   result.asks_for_help = true;
   result.verbose_count = 0;
   return result;
}

VerifiedCommandLineArguments VerifiedCommandLineArguments::fromConfigValuesAndPositionalArguments(
   std::unordered_map<ConfigKeyPath, ConfigValue> config_values,
   std::vector<std::string> positional_arguments,
   uint32_t verbose_count
) {
   VerifiedCommandLineArguments result;
   result.config_values = std::move(config_values);
   result.positional_arguments = std::move(positional_arguments);
   result.asks_for_help = false;
   result.verbose_count = verbose_count;
   return result;
}

}  // namespace rhydb::config
