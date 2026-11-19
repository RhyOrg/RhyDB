#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/database_config.h"
#include "rhydb/config/initialize_config.h"
#include "rhydb/database.h"
#include "rhydb/storage/reference_genomes.h"

namespace rhydb::initialize {

class Initializer {
  public:
   /// The name of the table in which preprocessing records which `reference_genomes` row each
   /// sequence column takes its reference sequence from, one row per sequence column. Unlike the
   /// built-in tables it is an ordinary user table: preprocessing creates it if it does not exist,
   /// but the database does not guarantee its existence or schema.
   static constexpr std::string_view COLUMN_REFERENCES_TABLE_NAME = "column_references";

   /// The columns of the column references table: the table and column of the sequence column, and
   /// the `name` and `type` of the `reference_genomes` row holding its reference sequence.
   static constexpr std::string_view COLUMN_REFERENCES_TABLE_NAME_COLUMN = "table_name";
   static constexpr std::string_view COLUMN_REFERENCES_COLUMN_NAME_COLUMN = "column_name";
   static constexpr std::string_view COLUMN_REFERENCES_REFERENCE_NAME_COLUMN = "reference_name";
   static constexpr std::string_view COLUMN_REFERENCES_REFERENCE_TYPE_COLUMN = "reference_type";

   static void createTableInDatabase(
      const schema::TableName& table_name,
      const config::InitializationFiles& initialization_files,
      Database& database
   );

   static void createTableInDatabase(
      const schema::TableName& table_name,
      const config::DatabaseConfig& database_config,
      const ReferenceGenomes& reference_genomes,
      const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
      const common::PhyloTree& phylo_tree,
      bool without_unaligned_sequences,
      Database& database
   );

   static std::shared_ptr<schema::TableSchema> createSchemaFromConfigFiles(
      const config::DatabaseConfig& database_config,
      ReferenceGenomes reference_genomes,
      const common::PhyloTree& phylo_tree_file,
      bool without_unaligned_sequences
   );

   static std::optional<common::LineageTreeAndIdMap> findLineageTreeForName(
      const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
      const std::string& lineage_tree_name
   );

   /// The name under which preprocessing the table `table_name` stores `name`: its reference
   /// sequences in the built-in reference genomes table and the lineage relation tables of its
   /// columns. That is `name` itself for the default table, so that `lineage(...)` resolves its
   /// lineage tables, and `<table_name>_<name>` for every other table, so that several tables can
   /// be preprocessed into the same database.
   [[nodiscard]] static std::string preprocessedName(
      const schema::TableName& table_name,
      std::string_view name
   );

  private:
   /// Adds the reference sequences of the table `preprocessed_table_name` to the built-in reference
   /// genomes table, named as given by `preprocessedName`.
   static void fillReferenceGenomesTable(
      const schema::TableName& preprocessed_table_name,
      const ReferenceGenomes& reference_genomes,
      Database& database
   );

   /// Adds one row per sequence column of the table `preprocessed_table_name` to the column
   /// references table, pointing at the rows added by `fillReferenceGenomesTable`. Creates the
   /// table if it does not exist, and throws if it exists with a different schema.
   static void fillColumnReferencesTable(
      const schema::TableName& preprocessed_table_name,
      const ReferenceGenomes& reference_genomes,
      Database& database
   );

   /// Adds one row per (lineage relation table name, lineage definition file) to the built-in
   /// lineage definitions table.
   static void fillLineageDefinitionsTable(
      const std::vector<std::pair<std::string, std::string>>& lineage_definitions,
      Database& database
   );

   static void createLineageRelationTable(
      std::string_view relation_table_name,
      const common::LineageTreeAndIdMap& lineage_tree,
      Database& database
   );

   static void createLineageAliasTable(
      std::string_view relation_table_name,
      const common::LineageTreeAndIdMap& lineage_tree,
      Database& database
   );
};
}  // namespace rhydb::initialize
