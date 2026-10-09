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

using rhydb::schema::ValueType;

ValueType rhydb::config::toDatabaseValueType(std::string_view type) {
   if (type == "string") {
      return ValueType::STRING;
   }
   if (type == "date") {
      return ValueType::DATE;
   }
   if (type == "boolean") {
      return ValueType::BOOL;
   }
   if (type == "int" || type == "int32") {
      return ValueType::INT32;
   }
   if (type == "int64") {
      return ValueType::INT64;
   }
   if (type == "float") {
      return ValueType::FLOAT;
   }

   throw rhydb::config::ConfigException("Unknown metadata type: " + std::string(type));
}

rhydb::config::LineageIndexType rhydb::config::toLineageIndexType(std::string_view type) {
   if (type == "columnMetadata") {
      return LineageIndexType::COLUMN_METADATA;
   }
   if (type == "table") {
      return LineageIndexType::TABLE;
   }
   if (type == "both") {
      return LineageIndexType::BOTH;
   }
   throw rhydb::config::ConfigException(
      "Unknown lineageIndexType: '" + std::string(type) +
      "'. Must be one of 'columnMetadata', 'table', 'both'."
   );
}

std::string_view rhydb::config::lineageIndexTypeToString(LineageIndexType type) {
   switch (type) {
      case LineageIndexType::COLUMN_METADATA:
         return "columnMetadata";
      case LineageIndexType::TABLE:
         return "table";
      case LineageIndexType::BOTH:
         return "both";
   }
   RHYDB_UNREACHABLE();
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
   metadata.type = rhydb::config::toDatabaseValueType(node["type"].as<std::string>());
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
   if (node["lineageIndexType"].IsDefined()) {
      metadata.lineage_index_type =
         rhydb::config::toLineageIndexType(node["lineageIndexType"].as<std::string>());
   } else {
      metadata.lineage_index_type = rhydb::config::LineageIndexType::COLUMN_METADATA;
   }
   if (node["isPhyloTreeField"].IsDefined()) {
      metadata.phylo_tree_node_identifier = node["isPhyloTreeField"].as<bool>();
   } else {
      metadata.phylo_tree_node_identifier = false;
   }
   if (node["treatUnknownLineagesAsNull"].IsDefined()) {
      metadata.treat_unknown_lineages_as_null = node["treatUnknownLineagesAsNull"].as<bool>();
   } else {
      metadata.treat_unknown_lineages_as_null = false;
   }
   return true;
}

using rhydb::schema::valueTypeToString;

YAML::Node YAML::convert<rhydb::config::DatabaseMetadata>::encode(
   const rhydb::config::DatabaseMetadata& metadata
) {
   Node node;
   node["name"] = metadata.name;
   node["type"] = std::string{valueTypeToString(metadata.type)};
   node["generateIndex"] = metadata.generate_index;
   if (metadata.generate_lineage_index) {
      node["generateLineageIndex"] = metadata.generate_lineage_index.value();
      node["lineageIndexType"] = std::string{lineageIndexTypeToString(metadata.lineage_index_type)};
   }
   if (metadata.phylo_tree_node_identifier) {
      node["isPhyloTreeField"] = true;
   }
   if (metadata.treat_unknown_lineages_as_null) {
      node["treatUnknownLineagesAsNull"] = true;
   }
   return node;
}

namespace rhydb::config {

schema::ColumnType DatabaseMetadata::getColumnType() const {
   if (type == ValueType::STRING) {
      if (generate_index) {
         return schema::ColumnType::DICTIONARY_ENCODED;
      }
      return schema::ColumnType::STRING;
   }
   if (type == ValueType::DATE) {
      return schema::ColumnType::DATE32;
   }
   if (type == ValueType::BOOL) {
      return schema::ColumnType::BOOL;
   }
   if (type == ValueType::INT32) {
      return schema::ColumnType::INT32;
   }
   if (type == ValueType::INT64) {
      return schema::ColumnType::INT64;
   }
   if (type == ValueType::FLOAT) {
      return schema::ColumnType::FLOAT;
   }
   throw std::runtime_error("Did not find metadata with name: " + std::string(name));
}

bool DatabaseMetadata::generatesLineageColumnIndex() const {
   return generate_lineage_index.has_value() && lineage_index_type != LineageIndexType::TABLE;
}

bool DatabaseMetadata::generatesLineageTable() const {
   return generate_lineage_index.has_value() &&
          lineage_index_type != LineageIndexType::COLUMN_METADATA;
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
std::map<std::string, ValueType> validateMetadataDefinitions(const DatabaseConfig& config) {
   std::map<std::string, ValueType> metadata_map;
   for (const auto& metadata : config.schema.metadata) {
      if (metadata_map.contains(metadata.name)) {
         throw ConfigException("Metadata " + metadata.name + " is defined twice in the config");
      }

      const auto generate_lineage_indexed_field = metadata.generate_lineage_index;
      if (metadata.type != ValueType::STRING && generate_lineage_indexed_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' generateLineageIndex is set, but the column is not of type STRING."
         );
      }

      const auto phylo_tree_node_identifiered_field = metadata.phylo_tree_node_identifier;
      if (metadata.type != ValueType::STRING && phylo_tree_node_identifiered_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' isPhyloTreeField is set, but the column is not of type STRING."
         );
      }

      const auto must_not_generate_index_on_type = metadata.type != ValueType::STRING;
      if (metadata.generate_index && must_not_generate_index_on_type) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' generateIndex is set, but generating an index is only allowed for types STRING"
         );
      }

      if (!metadata.generate_index && generate_lineage_indexed_field) {
         throw ConfigException(
            "Metadata '" + metadata.name +
            "' generateLineageIndex is set, generateIndex must also be set."
         );
      }

      if (
         !generate_lineage_indexed_field &&
         metadata.lineage_index_type != LineageIndexType::COLUMN_METADATA
      ) {
         throw ConfigException(
            "Metadata '" + metadata.name + "' lineageIndexType is set to '" +
            std::string(lineageIndexTypeToString(metadata.lineage_index_type)) +
            "', but generateLineageIndex is not set."
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
   const std::map<std::string, ValueType> metadata_map = validateMetadataDefinitions(config);

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
      valueTypeToString(database_metadata.type),
      database_metadata.generate_index
   );
}
