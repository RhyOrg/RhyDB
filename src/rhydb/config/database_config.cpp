#include "rhydb/config/database_config.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "config/config_exception.h"

using rhydb::schema::DataType;

DataType rhydb::config::toDatabaseDataType(std::string_view type) {
   const auto data_type = rhydb::schema::fromSchemaTypeName(type);
   // Sequence columns are declared separately, as `nucleotideSequences` etc.
   if (!data_type.has_value() || rhydb::schema::isSequenceColumn(data_type.value())) {
      throw ConfigException("Unknown metadata type: " + std::string(type));
   }
   return data_type.value();
}

bool YAML::convert<rhydb::config::DatabaseConfig>::decode(
   const Node& node,
   rhydb::config::DatabaseConfig& config
) {
   config.schema = node["schema"].as<rhydb::config::DatabaseSchema>();

   SPDLOG_TRACE("Resulting database config: {}", config);

   return true;
}
YAML::Node YAML::convert<rhydb::config::DatabaseConfig>::encode(
   const rhydb::config::DatabaseConfig& config
) {
   Node node;
   node["schema"] = config.schema;

   return node;
}

bool YAML::convert<rhydb::config::DatabaseSchema>::decode(
   const Node& node,
   rhydb::config::DatabaseSchema& schema
) {
   schema.instance_name = node["instanceName"].as<std::string>();
   if (node["primaryKey"].IsDefined() && !node["primaryKey"].IsNull()) {
      schema.primary_key = node["primaryKey"].as<std::string>();
   } else {
      schema.primary_key = std::nullopt;
   }

   if (!node["metadata"].IsSequence()) {
      return false;
   }

   for (const auto& metadata : node["metadata"]) {
      schema.metadata.push_back(metadata.as<rhydb::config::DatabaseMetadata>());
   }
   return true;
}

YAML::Node YAML::convert<rhydb::config::DatabaseSchema>::encode(
   const rhydb::config::DatabaseSchema& schema
) {
   Node node;
   node["instanceName"] = schema.instance_name;
   if (schema.primary_key.has_value()) {
      node["primaryKey"] = schema.primary_key.value();
   }
   node["metadata"] = schema.metadata;
   return node;
}

bool YAML::convert<rhydb::config::DatabaseMetadata>::decode(
   const Node& node,
   rhydb::config::DatabaseMetadata& metadata
) {
   metadata.name = node["name"].as<std::string>();
   metadata.type = rhydb::config::toDatabaseDataType(node["type"].as<std::string>());
   if (node["generateIndex"].IsDefined()) {
      metadata.generate_index = node["generateIndex"].as<bool>();
   } else {
      metadata.generate_index = false;
   }
   if (node["generateLineageIndex"].IsDefined()) {
      metadata.generate_lineage_index = node["generateLineageIndex"].as<std::string>();
   } else {
      metadata.generate_lineage_index = std::nullopt;
   }
   if (node["isPhyloTreeField"].IsDefined()) {
      metadata.phylo_tree_node_identifier = node["isPhyloTreeField"].as<bool>();
   } else {
      metadata.phylo_tree_node_identifier = false;
   }
   return true;
}

using rhydb::schema::toSchemaTypeName;

YAML::Node YAML::convert<rhydb::config::DatabaseMetadata>::encode(
   const rhydb::config::DatabaseMetadata& metadata
) {
   Node node;
   node["name"] = metadata.name;
   node["type"] = std::string{toSchemaTypeName(metadata.type)};
   node["generateIndex"] = metadata.generate_index;
   if (metadata.generate_lineage_index) {
      node["generateLineageIndex"] = metadata.generate_lineage_index.value();
   }
   if (metadata.phylo_tree_node_identifier) {
      node["isPhyloTreeField"] = true;
   }
   return node;
}

namespace rhydb::config {

schema::DataType DatabaseMetadata::getColumnType() const {
   if (generate_index) {
      return schema::DataType::dictionaryEncoded(type);
   }
   return type;
}

std::optional<DatabaseMetadata> DatabaseConfig::getMetadata(const std::string& name) const {
   auto element = std::ranges::find_if(schema.metadata, [&name](const auto& metadata) {
      return metadata.name == name;
   });
   if (element == std::end(schema.metadata)) {
      return std::nullopt;
   }
   return *element;
}

void DatabaseConfig::writeConfig(const std::filesystem::path& config_path) const {
   const YAML::Node node = YAML::convert<DatabaseConfig>::encode(*this);
   SPDLOG_DEBUG("Writing database config to {}", config_path.string());
   std::ofstream out_file(config_path);
   out_file << YAML::Dump(node);
}

DatabaseConfig DatabaseConfig::getValidatedConfig(const std::string& config_yaml) {
   auto config = YAML::Load(config_yaml).as<DatabaseConfig>();
   validateConfig(config);
   return config;
}

DatabaseConfig DatabaseConfig::getValidatedConfigFromFile(
   const std::filesystem::path& config_path
) {
   SPDLOG_INFO("Reading database config from {}", config_path.string());
   std::stringstream yaml;

   std::ifstream file(config_path);
   if (!file.is_open()) {
      throw std::runtime_error(
         "Failed to read database config: Could not open file " + config_path.string()
      );
   }

   if (file.peek() != std::ifstream::traits_type::eof()) {
      yaml << file.rdbuf();
   }

   try {
      return DatabaseConfig::getValidatedConfig(yaml.str());
   } catch (const YAML::Exception& e) {
      throw std::runtime_error(
         "Failed to read database config from " + config_path.string() + ": " +
         std::string(e.what())
      );
   }
}

namespace {
std::map<std::string, DataType> validateMetadataDefinitions(const DatabaseConfig& config) {
   std::map<std::string, DataType> metadata_map;
   for (const auto& metadata : config.schema.metadata) {
      if (metadata_map.contains(metadata.name)) {
         throw ConfigException("Metadata " + metadata.name + " is defined twice in the config");
      }

      const auto generate_lineage_indexed_field = metadata.generate_lineage_index;
      if (metadata.type != DataType::STRING && generate_lineage_indexed_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' generateLineageIndex is set, but the column is not of type STRING."
         );
      }

      const auto phylo_tree_node_identifiered_field = metadata.phylo_tree_node_identifier;
      if (metadata.type != DataType::STRING && phylo_tree_node_identifiered_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' isPhyloTreeField is set, but the column is not of type STRING."
         );
      }

      if (metadata.generate_index && !DataType::canBeDictionaryEncoded(metadata.type)) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' generateIndex is set, but generating an index is only allowed for types STRING "
            "and DATE"
         );
      }

      if (!metadata.generate_index && generate_lineage_indexed_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' generateLineageIndex is set, generateIndex must also be set."
         );
      }

      if (metadata.generate_index && phylo_tree_node_identifiered_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' isPhyloTreeField and generateIndex are both set, if isPhyloTreeField "
            "is "
            "set then generateIndex cannot be set."
         );
      }

      metadata_map[metadata.name] = metadata.type;
   }
   return metadata_map;
}
}  // namespace

void DatabaseConfig::validateConfig(const DatabaseConfig& config) {
   const std::map<std::string, DataType> metadata_map = validateMetadataDefinitions(config);

   if (config.schema.metadata.empty()) {
      throw ConfigException("Database config without fields not possible");
   }

   if (
      config.schema.primary_key.has_value() &&
      !metadata_map.contains(config.schema.primary_key.value())
   ) {
      throw ConfigException("Primary key is not in metadata");
   }
}

}  // namespace rhydb::config

[[maybe_unused]] auto fmt::formatter<rhydb::config::DatabaseConfig>::format(
   const rhydb::config::DatabaseConfig& database_config,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   return fmt::format_to(ctx.out(), "{{ schema: {} }}", database_config.schema);
}

[[maybe_unused]] auto fmt::formatter<rhydb::config::DatabaseSchema>::format(
   const rhydb::config::DatabaseSchema& database_schema,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   return fmt::format_to(
      ctx.out(),
      "{{ instance_name: '{}', primary_key: {}, metadata: [{}] }}",
      database_schema.instance_name,
      database_schema.primary_key.has_value()
         ? fmt::format("'{}'", database_schema.primary_key.value())
         : "none",
      fmt::join(database_schema.metadata, ",")
   );
}

[[maybe_unused]] auto fmt::formatter<rhydb::config::DatabaseMetadata>::format(
   const rhydb::config::DatabaseMetadata& database_metadata,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   return fmt::format_to(
      ctx.out(),
      "{{ name: '{}', type: '{}', generate_index: {} }}",
      database_metadata.name,
      toSchemaTypeName(database_metadata.type),
      database_metadata.generate_index
   );
}
