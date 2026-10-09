#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include "config/config_key_path.h"
#include "config/config_value.h"

namespace rhydb::config {

/// A VerifiedConfigAttributes is providing I/O-, key error and parse
/// error free access to a set of configuration data.
///
/// The accessors return an option since even though invalid options are
/// not present in this, the given option may also not be present.
///
/// `positional_arguments`, `asks_for_help`, and `verbose_count` are only used
/// by the command line argument backend, other backends leave them empty/false/0.
class VerifiedConfigAttributes {
  public:
   std::unordered_map<ConfigKeyPath, ConfigValue> config_values;

   [[nodiscard]] std::optional<std::string> getString(const ConfigKeyPath& config_key_path) const;

   [[nodiscard]] std::optional<std::filesystem::path> getPath(
      const ConfigKeyPath& config_key_path
   ) const;

   [[nodiscard]] std::optional<int32_t> getInt32(const ConfigKeyPath& config_key_path) const;

   [[nodiscard]] std::optional<uint32_t> getUint32(const ConfigKeyPath& config_key_path) const;

   [[nodiscard]] std::optional<uint16_t> getUint16(const ConfigKeyPath& config_key_path) const;

   [[nodiscard]] std::optional<bool> getBool(const ConfigKeyPath& config_key_path) const;

   [[nodiscard]] std::optional<std::vector<std::string>> getList(
      const ConfigKeyPath& config_key_path
   ) const;
};

class VerifiedCommandLineArguments : public VerifiedConfigAttributes {
  public:
   std::vector<std::string> positional_arguments;
   bool asks_for_help;
   /// Number of times -v / --verbose was passed on the command line.
   /// 1 → debug level, 2+ → trace level.
   uint32_t verbose_count;

   static VerifiedCommandLineArguments askingForHelp();

   static VerifiedCommandLineArguments fromConfigValuesAndPositionalArguments(
      std::unordered_map<ConfigKeyPath, ConfigValue> config_values,
      std::vector<std::string> positional_arguments,
      uint32_t verbose_count
   );
};

}  // namespace rhydb::config
