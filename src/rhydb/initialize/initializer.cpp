#include "rhydb/initialize/initializer.h"

#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/append/ndjson_line_reader.h"
#include "rhydb/append/table_inserter.h"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initialize_exception.h"
#include "rhydb/initialize/lineage_relation_table.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/column_type_visitor.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"
#include "rhydb/storage/reference_genomes.h"

namespace rhydb::initialize {

void Initializer::createTableInDatabase(
   schema::TableName table_name,
   const config::InitializationFiles& initialization_files,
   Database& database
) {
   EVOBENCH_SCOPE("Initializer", "initializeDatabase");
   std::map<std::filesystem::path, common::LineageTreeAndIdMap> lineage_trees;
   for (const auto& file_path : initialization_files.getLineageDefinitionFilepaths()) {
      lineage_trees[file_path] =
         common::LineageTreeAndIdMap::fromLineageDefinitionFilePath(file_path);
   }

   common::PhyloTree phylo_tree_file;
   auto opt_path = initialization_files.getPhyloTreeFilepath();
   if (opt_path.has_value()) {
      phylo_tree_file = common::PhyloTree::fromFile(opt_path.value());
   }

   createTableInDatabase(
      std::move(table_name),
      config::DatabaseConfig::getValidatedConfigFromFile(
         initialization_files.getDatabaseConfigFilepath()
      ),
      ReferenceGenomes::readFromFile(initialization_files.getReferenceGenomeFilepath()),
      lineage_trees,
      phylo_tree_file,
      initialization_files.without_unaligned_sequences,
      database
   );
}

void Initializer::createTableInDatabase(
   schema::TableName table_name,
   const config::DatabaseConfig& database_config,
   const ReferenceGenomes& reference_genomes,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
   const common::PhyloTree& phylo_tree,
   bool without_unaligned_sequences,
   Database& database
) {
   auto table_schema = createSchemaFromConfigFiles(
      database_config, reference_genomes, lineage_trees, phylo_tree, without_unaligned_sequences
   );
   database.createTable(std::move(table_name), std::move(table_schema));

   createReferenceGenomesTable(reference_genomes, database);

   // Materialize a companion lineage relation table for every column configured with
   // `lineageIndexType` 'table' or 'both'. The tree name is resolved against the loaded lineage
   // definitions the same way the column's in-memory index is.
   for (const auto& config_metadata : database_config.schema.metadata) {
      if (!config_metadata.generatesLineageTable()) {
         continue;
      }
      auto lineage_tree =
         findLineageTreeForName(lineage_trees, config_metadata.generate_lineage_index.value());
      if (!lineage_tree.has_value()) {
         auto keys =
            lineage_trees | std::views::keys |
            std::views::transform([](const std::filesystem::path& path) { return path.string(); });
         throw InitializeException(
            "Column '{}' has lineage tree '{}' configured, but did not find corresponding lineage "
            "tree in the provided lineageDefinitionFilenames: {}",
            config_metadata.name,
            config_metadata.generate_lineage_index.value(),
            fmt::join(keys, ",")
         );
      }
      createLineageRelationTable(config_metadata.name, lineage_tree.value(), database);
   }
}

void Initializer::createReferenceGenomesTable(
   const ReferenceGenomes& reference_genomes,
   Database& database
) {
   const std::string table_name_string{REFERENCE_GENOMES_TABLE_NAME};
   const schema::TableName table_name{table_name_string};
   if (database.tables.contains(table_name)) {
      throw InitializeException(
         "Cannot create reference genomes table '{}': a table with that name already exists.",
         table_name_string
      );
   }

   // No primary key: a nucleotide and an amino acid sequence may share a name, so only (name, type)
   // identifies a row.
   const schema::ColumnIdentifier name_column{.name = "name", .type = schema::ColumnType::STRING};
   // Either "nucleotide" or "amino_acid".
   const schema::ColumnIdentifier type_column{.name = "type", .type = schema::ColumnType::STRING};
   const schema::ColumnIdentifier sequence_column{
      .name = "sequence", .type = schema::ColumnType::STRING,
   };
   auto table_schema = std::make_shared<schema::TableSchema>();
   table_schema->column_metadata.emplace(
      name_column, std::make_shared<storage::column::StringColumnMetadata>(name_column.name)
   );
   table_schema->column_metadata.emplace(
      type_column, std::make_shared<storage::column::StringColumnMetadata>(type_column.name)
   );
   table_schema->column_metadata.emplace(
      sequence_column, std::make_shared<storage::column::StringColumnMetadata>(sequence_column.name)
   );
   database.createTable(table_name, std::move(table_schema));

   std::string ndjson;
   const auto append_rows = [&ndjson](
                               const std::vector<std::string>& names,
                               const std::vector<std::string>& sequences,
                               std::string_view type
                            ) {
      for (size_t index = 0; index < names.size(); ++index) {
         const nlohmann::json line{
            {"name", names.at(index)}, {"type", type}, {"sequence", sequences.at(index)},
         };
         ndjson += line.dump();
         ndjson += '\n';
      }
   };
   append_rows(
      reference_genomes.nucleotide_sequence_names,
      reference_genomes.raw_nucleotide_sequences,
      "nucleotide"
   );
   append_rows(
      reference_genomes.aa_sequence_names, reference_genomes.raw_aa_sequences, "amino_acid"
   );
   std::stringstream ndjson_stream{ndjson};
   rhydb::append::NdjsonLineReader ndjson_reader{ndjson_stream};
   rhydb::append::appendDataToTable(database.tables.at(table_name), ndjson_reader);
   SPDLOG_INFO(
      "Built reference genomes table '{}' with {} rows",
      table_name_string,
      reference_genomes.nucleotide_sequence_names.size() +
         reference_genomes.aa_sequence_names.size()
   );
}

void Initializer::createLineageRelationTable(
   std::string_view column_name,
   const common::LineageTreeAndIdMap& lineage_tree,
   Database& database
) {
   const std::string table_name_string{column_name};
   const schema::TableName table_name{table_name_string};
   if (database.tables.contains(table_name)) {
      throw InitializeException(
         "Cannot create lineage relation table '{}': a table with that name already exists.",
         table_name_string
      );
   }

   const schema::ColumnIdentifier id_column{.name = "id", .type = schema::ColumnType::STRING};
   const schema::ColumnIdentifier lineage_column{
      .name = "lineage", .type = schema::ColumnType::STRING,
   };
   // The direct parent of `lineage` (null for a root). The transitive ancestry is walked from
   // these edges at query time rather than materialized.
   const schema::ColumnIdentifier parent_column{
      .name = "parent", .type = schema::ColumnType::STRING,
   };
   // True when `lineage` has more than one direct parent (an edge into a recombinant node).
   const schema::ColumnIdentifier is_recombinant_edge_column{
      .name = "is_recombinant_edge", .type = schema::ColumnType::BOOL,
   };
   // For a recombinant `lineage`, the most-recent common ancestor of its parents; null
   // otherwise.
   const schema::ColumnIdentifier recombinant_clade_ancestor_column{
      .name = "recombinant_clade_ancestor", .type = schema::ColumnType::STRING,
   };
   auto table_schema = std::make_shared<schema::TableSchema>();
   table_schema->column_metadata.emplace(
      id_column, std::make_shared<storage::column::StringColumnMetadata>(id_column.name)
   );
   table_schema->column_metadata.emplace(
      lineage_column, std::make_shared<storage::column::StringColumnMetadata>(lineage_column.name)
   );
   table_schema->column_metadata.emplace(
      parent_column, std::make_shared<storage::column::StringColumnMetadata>(parent_column.name)
   );
   table_schema->column_metadata.emplace(
      is_recombinant_edge_column,
      std::make_shared<storage::column::ColumnMetadata>(is_recombinant_edge_column.name)
   );
   table_schema->column_metadata.emplace(
      recombinant_clade_ancestor_column,
      std::make_shared<storage::column::StringColumnMetadata>(recombinant_clade_ancestor_column.name
      )
   );
   table_schema->primary_key = id_column;
   database.createTable(table_name, std::move(table_schema));

   const auto rows = buildLineageRelationRows(lineage_tree);
   std::string ndjson;
   size_t row_id = 0;
   for (const auto& row : rows) {
      const nlohmann::json line{
         {"id", std::to_string(row_id++)},
         {"lineage", row.lineage},
         {"parent", row.parent.has_value() ? nlohmann::json(*row.parent) : nlohmann::json(nullptr)},
         {"is_recombinant_edge", row.is_recombinant_edge},
         {"recombinant_clade_ancestor",
          row.recombinant_clade_ancestor.has_value()
             ? nlohmann::json(*row.recombinant_clade_ancestor)
             : nlohmann::json(nullptr),},
      };
      ndjson += line.dump();
      ndjson += '\n';
   }
   std::stringstream ndjson_stream{ndjson};
   rhydb::append::NdjsonLineReader ndjson_reader{ndjson_stream};
   rhydb::append::appendDataToTable(database.tables.at(table_name), ndjson_reader);
   SPDLOG_INFO("Built lineage relation table '{}' with {} rows", table_name_string, rows.size());
}

struct ColumnMetadataInitializer {
   template <storage::column::Column ColumnType>
   void operator()(
      std::shared_ptr<storage::column::ColumnMetadata>& metadata,
      const config::DatabaseMetadata& config_metadata,
      const ReferenceGenomes& reference_genomes,
      const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
      const common::PhyloTree& phylo_tree_file
   );
};

template <>
void ColumnMetadataInitializer::operator()<storage::column::DictionaryEncodedColumn>(
   std::shared_ptr<storage::column::ColumnMetadata>& metadata,
   const config::DatabaseMetadata& config_metadata,
   const ReferenceGenomes& /*reference_genomes*/,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
   const common::PhyloTree& /*phylo_tree_file*/
) {
   if (config_metadata.generatesLineageColumnIndex()) {
      auto lineage_tree_name = config_metadata.generate_lineage_index.value();
      auto lineage_tree = Initializer::findLineageTreeForName(lineage_trees, lineage_tree_name);
      if (not lineage_tree.has_value()) {
         auto keys =
            lineage_trees | std::views::keys |
            std::views::transform([](const std::filesystem::path& path) { return path.string(); });
         throw InitializeException(
            "Column '{}' has lineage tree '{}' configured, but did not find corresponding lineage "
            "tree in the provided lineageDefinitionFilenames: {}",
            config_metadata.name,
            config_metadata.generate_lineage_index.value(),
            fmt::join(keys, ",")
         );
      }
      metadata = std::make_shared<storage::column::DictionaryEncodedColumn::Metadata>(
         config_metadata.name, lineage_tree.value(), config_metadata.treat_unknown_lineages_as_null
      );
   } else {
      metadata =
         std::make_shared<storage::column::DictionaryEncodedColumn::Metadata>(config_metadata.name);
   }
}

template <>
void ColumnMetadataInitializer::operator()<storage::column::StringColumn>(
   std::shared_ptr<storage::column::ColumnMetadata>& metadata,
   const config::DatabaseMetadata& config_metadata,
   const ReferenceGenomes& /*reference_genomes*/,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& /*lineage_trees*/,
   const common::PhyloTree& phylo_tree_file
) {
   if (config_metadata.phylo_tree_node_identifier) {
      metadata = std::make_shared<storage::column::StringColumn::Metadata>(
         config_metadata.name, phylo_tree_file
      );
   } else {
      metadata = std::make_shared<storage::column::StringColumn::Metadata>(config_metadata.name);
   }
}

template <>
void ColumnMetadataInitializer::operator()<storage::column::ZstdCompressedStringColumn>(
   std::shared_ptr<storage::column::ColumnMetadata>& /*metadata*/,
   const config::DatabaseMetadata& /*config_metadata*/,
   const ReferenceGenomes& /*reference_genomes*/,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& /*lineage_trees*/,
   const common::PhyloTree& /*phylo_tree_file*/
) {
   RHYDB_PANIC("unaligned nucleotide sequences cannot be in config::DatabaseMetadata");
}

template <>
void ColumnMetadataInitializer::operator()<storage::column::SequenceColumn<Nucleotide>>(
   std::shared_ptr<storage::column::ColumnMetadata>& /*metadata*/,
   const config::DatabaseMetadata& /*config_metadata*/,
   const ReferenceGenomes& /*reference_genomes*/,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& /*lineage_trees*/,
   const common::PhyloTree& /*phylo_tree_file*/
) {
   RHYDB_PANIC("nucleotides cannot be in config::DatabaseMetadata");
}

template <>
void ColumnMetadataInitializer::operator()<storage::column::SequenceColumn<AminoAcid>>(
   std::shared_ptr<storage::column::ColumnMetadata>& /*metadata*/,
   const config::DatabaseMetadata& /*config_metadata*/,
   const ReferenceGenomes& /*reference_genomes*/,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& /*lineage_trees*/,
   const common::PhyloTree& /*phylo_tree_file*/
) {
   RHYDB_PANIC("amino acid cannot be in config::DatabaseMetadata");
}

template <storage::column::Column ColumnType>
void ColumnMetadataInitializer::operator()(
   std::shared_ptr<storage::column::ColumnMetadata>& metadata,
   const config::DatabaseMetadata& config_metadata,
   const ReferenceGenomes& /*reference_genomes*/,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& /*lineage_trees*/,
   const common::PhyloTree& /*phylo_tree_file*/
) {
   metadata = std::make_shared<typename ColumnType::Metadata>(config_metadata.name);
}

namespace {

void assertPrimaryKeyInMetadata(const rhydb::config::DatabaseConfig& database_config) {
   if (!database_config.schema.primary_key.has_value()) {
      return;
   }
   auto primary_key_metadata = std::ranges::find_if(
      database_config.schema.metadata,
      [&database_config](const auto& metadata) {
         return database_config.schema.primary_key.value() == metadata.name;
      }
   );
   if (primary_key_metadata == database_config.schema.metadata.end()) {
      throw InitializeException("The primary key is not contained in the metadata.");
   }
}

void assertPrimaryKeyOfTypeString(const rhydb::config::DatabaseConfig& database_config) {
   if (!database_config.schema.primary_key.has_value()) {
      return;
   }
   auto primary_key_metadata = std::ranges::find_if(
      database_config.schema.metadata,
      [&database_config](const auto& metadata) {
         return database_config.schema.primary_key.value() == metadata.name;
      }
   );
   auto primary_key_type = primary_key_metadata->getColumnType();
   if (primary_key_type != schema::ColumnType::STRING) {
      throw InitializeException(
         "The primary key must be of type STRING but it is of type {}",
         columnTypeToString(primary_key_type)
      );
   }
}

// TODO(#741) we prepend the unalignedSequence columns (which are using the type
// ZstdCompressedStringColumn) with 'unaligned_'. This should be cleaned up with a
// refactor and breaking change of the current input format.
const std::string UNALIGNED_NUCLEOTIDE_SEQUENCE_PREFIX = "unaligned_";

}  // namespace

std::shared_ptr<schema::TableSchema> Initializer::createSchemaFromConfigFiles(
   const config::DatabaseConfig& database_config,
   ReferenceGenomes reference_genomes,
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
   const common::PhyloTree& phylo_tree_file,
   bool without_unaligned_sequences
) {
   assertPrimaryKeyInMetadata(database_config);
   assertPrimaryKeyOfTypeString(database_config);

   const std::optional<schema::ColumnIdentifier> primary_key =
      database_config.schema.primary_key.transform([](const std::string& name) {
         return schema::ColumnIdentifier{.name = name, .type = schema::ColumnType::STRING};
      });

   std::map<schema::ColumnIdentifier, std::shared_ptr<storage::column::ColumnMetadata>>
      column_metadata;
   for (const auto& config_metadata : database_config.schema.metadata) {
      const schema::ColumnIdentifier column_identifier{
         .name = config_metadata.name, .type = config_metadata.getColumnType(),
      };
      std::shared_ptr<storage::column::ColumnMetadata> metadata;
      storage::column::visit(
         column_identifier.type,
         ColumnMetadataInitializer{},
         metadata,
         config_metadata,
         reference_genomes,
         lineage_trees,
         phylo_tree_file
      );
      column_metadata.emplace(column_identifier, metadata);
   }

   for (size_t sequence_idx = 0; sequence_idx < reference_genomes.nucleotide_sequence_names.size();
        ++sequence_idx) {
      const auto& sequence_name = reference_genomes.nucleotide_sequence_names.at(sequence_idx);
      const auto& reference_sequence = reference_genomes.raw_nucleotide_sequences.at(sequence_idx);
      const schema::ColumnIdentifier column_identifier{
         .name = sequence_name, .type = schema::ColumnType::NUCLEOTIDE_SEQUENCE,
      };
      auto metadata = std::make_shared<storage::column::SequenceColumnMetadata<Nucleotide>>(
         sequence_name, ReferenceGenomes::stringToVector<Nucleotide>(reference_sequence)
      );
      column_metadata.emplace(column_identifier, std::move(metadata));

      if (!without_unaligned_sequences) {
         const schema::ColumnIdentifier column_identifier_unaligned{
            .name = UNALIGNED_NUCLEOTIDE_SEQUENCE_PREFIX + sequence_name,
            .type = schema::ColumnType::ZSTD_COMPRESSED_STRING,
         };
         auto metadata_unaligned =
            std::make_shared<storage::column::ZstdCompressedStringColumnMetadata>(
               sequence_name, reference_sequence
            );
         column_metadata.emplace(column_identifier_unaligned, std::move(metadata_unaligned));
      }
   }

   for (size_t sequence_idx = 0; sequence_idx < reference_genomes.aa_sequence_names.size();
        ++sequence_idx) {
      const auto& sequence_name = reference_genomes.aa_sequence_names.at(sequence_idx);
      const auto& reference_sequence = reference_genomes.raw_aa_sequences.at(sequence_idx);
      const schema::ColumnIdentifier column_identifier{
         .name = sequence_name, .type = schema::ColumnType::AMINO_ACID_SEQUENCE,
      };
      auto metadata = std::make_shared<storage::column::SequenceColumnMetadata<AminoAcid>>(
         sequence_name, ReferenceGenomes::stringToVector<AminoAcid>(reference_sequence)
      );
      column_metadata.emplace(column_identifier, std::move(metadata));
   }

   return std::make_shared<schema::TableSchema>(column_metadata, primary_key);
}

std::optional<common::LineageTreeAndIdMap> Initializer::findLineageTreeForName(
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
   const std::string& lineage_tree_name
) {
   for (const auto& [path, lineage_tree] : lineage_trees) {
      if (path.filename() == lineage_tree_name || path.filename() == lineage_tree_name + ".yaml") {
         return lineage_tree;
      }
   }
   return std::nullopt;
}

}  // namespace rhydb::initialize
