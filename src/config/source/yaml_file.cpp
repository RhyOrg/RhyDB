#include "config/source/yaml_file.h"

#include <algorithm>
#include <fstream>

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

#include "config/config_exception.h"
#include "rhydb/common/cons_list.h"

using rhydb::config::ConfigKeyPath;

namespace {

bool isSequenceOfScalars(const YAML::Node& node) {
   if (!node.IsSequence()) {
      return false;
   }
   return std::ranges::all_of(node, [](const auto& element) { return element.IsScalar(); });
}

bool isProperSingularValue(const YAML::Node& node) {
   if (node.IsMap()) {
      SPDLOG_TRACE("isProperSingularValue = false, node is a map");
      return false;
   }
   if (!node.IsDefined()) {
      SPDLOG_TRACE("isProperSingularValue = false, node is not defined");
      return false;
   }
   if (!node.IsScalar() && !isSequenceOfScalars(node)) {
      SPDLOG_TRACE("isProperSingularValue = false, node is not a scalar or sequence of strings");
      return false;
   }
   return true;
}

std::vector<std::string> splitByDot(const std::string& str) {
   std::vector<std::string> result;
   boost::split(result, str, boost::is_any_of("."));
   return result;
}

std::vector<std::string> splitCamelCase(const std::string& camelCaseString) {
   std::vector<std::string> result;
   std::string current;

   for (const char character : camelCaseString) {
      if (std::isupper(character)) {
         result.push_back(current);
         current.clear();

         current += static_cast<char>(std::tolower(character));
      } else {
         current += character;
      }
   }
   if (!current.empty()) {
      result.push_back(current);
   }

   return result;
}

std::string joinCamelCase(const std::vector<std::string>& words) {
   std::string camel_case_string;

   for (size_t i = 0; i < words.size(); ++i) {
      if (i == 0) {
         camel_case_string += words[i];
      } else {
         std::string word = words[i];
         if (!word.empty()) {
            word[0] = static_cast<char>(std::toupper(word[0]));
            camel_case_string += word;
         }
      }
   }

   return camel_case_string;
}

// NOLINTNEXTLINE(misc-no-recursion)
void yamlToPaths(
   const std::string& debug_context,
   const YAML::Node& node,
   const ConsList<std::vector<std::string>>& parents,
   std::unordered_map<ConfigKeyPath, YAML::Node>& paths
) {
   if (node.IsMap()) {
      for (const auto& key_value : node) {
         try {
            const auto key = key_value.first.as<std::string>();
            const auto parents2 = parents.cons(splitCamelCase(key));
            const auto child_node = key_value.second;
            yamlToPaths(debug_context, child_node, parents2, paths);
         } catch (YAML::BadConversion& bad_conversion) {
            throw rhydb::config::ConfigException(
               fmt::format(
                  "invalid (non-literal) key in yaml config file '{}': {}",
                  debug_context,
                  bad_conversion.what()
               )
            );
         }
      }
   } else {
      auto parents_vector = parents.toVecReverse();
      auto path = ConfigKeyPath::tryFrom(parents_vector);
      if (!path) {
         // transform the ConsList parents into a debug string by reversing it,
         // transforming the elements into camel-case and joining them with a '.'
         auto debug_string_parents = ({
            std::vector<std::string> result;
            std::ranges::transform(parents_vector, std::back_inserter(result), joinCamelCase);
            boost::join(result, ".");
         });
         throw rhydb::config::ConfigException(
            fmt::format("{}: found invalid key: {}", debug_context, debug_string_parents)
         );
      }
      if (isProperSingularValue(node)) {
         paths.emplace(path.value(), node);
      } else {
         throw rhydb::config::ConfigException(
            fmt::format(
               "{}: found non-usable leaf value at nesting {}",
               debug_context,
               rhydb::config::YamlFile::configKeyPathToString(path.value())
            )
         );
      }
   }
}

}  // namespace

namespace rhydb::config {

std::string YamlFile::configKeyPathToString(const ConfigKeyPath& config_key_path) {
   std::vector<std::string> camel_case_strings;
   for (const auto& list : config_key_path.getPath()) {
      camel_case_strings.emplace_back(joinCamelCase(list));
   }
   return boost::join(camel_case_strings, ".");
}

ConfigKeyPath YamlFile::stringToConfigKeyPath(const std::string& key_path_string) {
   const std::vector<std::string> camel_case_strings = splitByDot(key_path_string);
   std::vector<std::vector<std::string>> result_path;
   std::ranges::transform(camel_case_strings, std::back_inserter(result_path), splitCamelCase);
   auto result = ConfigKeyPath::tryFrom(result_path);
   if (result == std::nullopt) {
      throw ConfigException(fmt::format("'{}' is not a valid YamlPath", key_path_string));
   }
   return result.value();
}

YamlFile YamlFile::fromYAML(const std::string& debug_context, const std::string& yaml_string) {
   try {
      const YAML::Node node = YAML::Load(yaml_string);

      // Collect all paths present
      std::unordered_map<ConfigKeyPath, YAML::Node> paths;
      yamlToPaths(debug_context, node, ConsList<std::vector<std::string>>{}, paths);

      return YamlFile{debug_context, paths};
   } catch (const YAML::ParserException& parser_exception) {
      throw std::runtime_error(
         fmt::format("{} does not contain valid YAML: {}", debug_context, parser_exception.what())
      );
   }
}

YamlFile YamlFile::readFile(const std::filesystem::path& path) {
   std::ifstream file(path, std::ios::in | std::ios::binary);
   if (file.fail()) {
      throw std::runtime_error(fmt::format("Could not open the YAML file: '{}'", path.string()));
   }

   std::ostringstream contents;
   if (file.peek() != std::ifstream::traits_type::eof()) {
      contents << file.rdbuf();
   }
   if (contents.fail()) {
      throw std::runtime_error(
         fmt::format("Error when reading the YAML file: '{}'", path.string())
      );
   }

   return fromYAML(fmt::format("file: '{}'", path.string()), contents.str());
}

std::string YamlFile::debugContext() const {
   return fmt::format("YAML file '{}'", debug_context);
}

namespace {
ConfigValue yamlNodeToConfigValue(
   const ConfigAttributeSpecification& attribute_spec,
   const YAML::Node& yaml
) {
   try {
      switch (attribute_spec.type) {
         case ConfigValueType::STRING:
            return ConfigValue::fromString(yaml.as<std::string>());
         case ConfigValueType::PATH:
            return ConfigValue::fromPath({std::filesystem::path{yaml.as<std::string>()}});
         case ConfigValueType::INT32:
            return ConfigValue::fromInt32(yaml.as<int32_t>());
         case ConfigValueType::UINT32:
            return ConfigValue::fromUint32(yaml.as<uint32_t>());
         case ConfigValueType::UINT16:
            return ConfigValue::fromUint16(yaml.as<uint16_t>());
         case ConfigValueType::BOOL:
            return ConfigValue::fromBool(yaml.as<bool>());
         case ConfigValueType::LIST:
            return ConfigValue::fromList(yaml.as<std::vector<std::string>>());
      }
      RHYDB_UNREACHABLE();
   } catch (YAML::BadConversion& error) {
      throw ConfigException(
         fmt::format(
            "cannot parse '{}' as {}: {}",
            YAML::Dump(yaml),
            configValueTypeToString(attribute_spec.type),
            error.what()
         )
      );
   }
}
}  // namespace

VerifiedConfigAttributes YamlFile::verify(const ConfigSpecification& config_specification) const {
   // No need to stringify and do duplicate check since
   // ConfigKeyPath is actually directly representing YAML paths.

   // Check the ones given, collect erroneous ones in foo.bar syntax
   std::vector<std::string> invalid_config_keys;
   std::unordered_map<ConfigKeyPath, ConfigValue> provided_config_values;
   for (const auto& [key, yaml_node] : getYamlFields()) {
      auto attribute_spec = config_specification.getAttributeSpecification(key);
      if (!attribute_spec.has_value()) {
         invalid_config_keys.push_back(configKeyPathToString(key));
      } else {
         const ConfigValue value = yamlNodeToConfigValue(attribute_spec.value(), yaml_node);
         provided_config_values.emplace(key, value);
      }
   }

   if (!invalid_config_keys.empty()) {
      const char* keys_or_options = (invalid_config_keys.size() >= 2) ? "keys" : "key";
      throw rhydb::config::ConfigException(
         fmt::format(
            "in {}: unknown {} {}",
            debugContext(),
            keys_or_options,
            boost::join(invalid_config_keys, ", ")
         )
      );
   }

   return VerifiedConfigAttributes{provided_config_values};
}

const std::unordered_map<ConfigKeyPath, YAML::Node>& YamlFile::getYamlFields() const {
   return yaml_fields;
}

}  // namespace rhydb::config
