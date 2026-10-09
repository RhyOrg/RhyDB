#include "config/config_specification.h"

#include <sstream>
#include <string>
#include <vector>

#include <boost/lexical_cast.hpp>

#include "config/config_key_path.h"
#include "config/source/command_line_arguments.h"
#include "config/source/environment_variables.h"
#include "config/source/yaml_file.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/string_utils.h"

namespace {
std::string indent(std::string_view indentation, const std::string& str) {
   auto lines = rhydb::splitBy(str, "\n");
   std::string out{};
   for (const auto& line : lines) {
      out.append(indentation);
      out.append(line);
      out.push_back('\n');
   }
   return out;
}
}  // namespace

namespace rhydb::config {

std::optional<ConfigAttributeSpecification> ConfigSpecification::
   getAttributeSpecificationFromAmbiguousKey(const AmbiguousConfigKeyPath& key) const {
   auto maybe_result =
      std::ranges::find_if(attribute_specifications, [&](const auto& attribute_spec) {
         return AmbiguousConfigKeyPath::from(attribute_spec.key) == key;
      });
   if (maybe_result == attribute_specifications.end()) {
      return std::nullopt;
   }
   return *maybe_result;
}

std::optional<ConfigAttributeSpecification> ConfigSpecification::getAttributeSpecification(
   const ConfigKeyPath& key
) const {
   auto maybe_result =
      std::ranges::find_if(attribute_specifications, [&](const auto& attribute_spec) {
         return attribute_spec.key == key;
      });
   if (maybe_result == attribute_specifications.end()) {
      return std::nullopt;
   }
   return *maybe_result;
}

std::string ConfigSpecification::helpText() const {
   std::ostringstream help_text;
   help_text << "Usage: " << program_name << " [options...]\n"
             << "\n"
             << "  Showing the options for `" << program_name
             << "`. To see the options for the other\n"
             << "  modes, use 'rhydb <mode> --help'.\n"
             << "\n"
             << "  Options override environment variables which override YAML file entries.\n"
             << "  The following options are valid:\n"
             << "\n"
             << "  -h | --help\n"
             << "\n"
             << "    Show help.\n"
             << "\n"
             << "  -v | --verbose\n"
             << "\n"
             << "    Increase log verbosity. Pass once for debug level, twice for trace level.\n";
   auto addln = [&help_text](const std::string& line) { help_text << line << "\n"; };

   for (const auto& field_spec : attribute_specifications) {
      addln("");
      const std::string_view type_text = field_spec.type == ConfigValueType::BOOL
                                            ? " (boolean, the option implies 'true')"
                                            : configValueTypeToString(field_spec.type);
      addln(
         fmt::format(
            "  {} {}", CommandLineArguments::configKeyPathToString(field_spec.key), type_text
         )
      );
      addln(
         fmt::format(
            "    Env var : {}", EnvironmentVariables::configKeyPathToString(field_spec.key)
         )
      );
      addln(fmt::format("    YAML key: {}", YamlFile::configKeyPathToString(field_spec.key)));
      addln("\n" + indent(std::string_view{"    "}, std::string{field_spec.help_text}));
      addln(
         field_spec.default_value.has_value()
            ? fmt::format("    Default: {}", field_spec.default_value->toString())
            : "    No default."
      );
   }

   return help_text.str();
}

VerifiedConfigAttributes ConfigSpecification::getConfigSourceFromDefaults() const {
   VerifiedConfigAttributes result;
   for (const auto& attribute_spec : attribute_specifications) {
      if (attribute_spec.default_value.has_value()) {
         result.config_values.emplace(attribute_spec.key, attribute_spec.default_value.value());
      }
   }
   return result;
}

ConfigValue ConfigAttributeSpecification::parseValueFromString(std::string value_string) const {
   try {
      switch (type) {
         case ConfigValueType::STRING:
            return ConfigValue::fromString(value_string);
         case ConfigValueType::PATH: {
            return ConfigValue::fromPath(value_string);
         }
         case ConfigValueType::UINT32: {
            // Because boost does not error on negative numbers
            if (value_string.starts_with('-')) {
               throw ConfigException(
                  fmt::format(
                     "cannot parse negative number '{}' as unsigned type {}",
                     value_string,
                     configValueTypeToString(type)
                  )
               );
            }
            const auto parsed_unsigned = boost::lexical_cast<uint32_t>(value_string);
            return ConfigValue::fromUint32(parsed_unsigned);
         }
         case ConfigValueType::UINT16: {
            // Because boost does not error on negative numbers
            if (value_string.starts_with('-')) {
               throw ConfigException(
                  fmt::format(
                     "cannot parse negative number '{}' as unsigned type {}",
                     value_string,
                     configValueTypeToString(type)
                  )
               );
            }
            const auto parsed_unsigned = boost::lexical_cast<uint16_t>(value_string);
            return ConfigValue::fromUint16(parsed_unsigned);
         }
         case ConfigValueType::INT32: {
            const auto parsed_signed = boost::lexical_cast<int32_t>(value_string);
            return ConfigValue::fromInt32(parsed_signed);
         }
         case ConfigValueType::BOOL:
            if (value_string == "true" || value_string == "1") {
               return ConfigValue::fromBool(true);
            }
            if (value_string == "false" || value_string == "0") {
               return ConfigValue::fromBool(false);
            }
            throw ConfigException(
               fmt::format("'{}' is not a valid string for a boolean", value_string)
            );
         case ConfigValueType::LIST:
            throw ConfigException("List values can currently no be specified as strings.");
      }
      RHYDB_UNREACHABLE();
   } catch (boost::bad_lexical_cast&) {
      throw ConfigException(
         fmt::format("cannot parse '{}' as {}", value_string, configValueTypeToString(type))
      );
   }
}

}  // namespace rhydb::config
