#pragma once

#include <string_view>

#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/database_config.h"
#include "rhydb/config/initialize_config.h"
#include "rhydb/database.h"
#include "rhydb/storage/reference_genomes.h"

namespace rhydb::initialize {

class Initializer {
  public:
   static void createTableInDatabase(
      schema::TableName table_name,
      const config::InitializationFiles& initialization_files,
      Database& database
   );

   static void createTableInDatabase(
      schema::TableName table_name,
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
      const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
      const common::PhyloTree& phylo_tree_file,
      bool without_unaligned_sequences
   );

   static std::optional<common::LineageTreeAndIdMap> findLineageTreeForName(
      const std::map<std::filesystem::path, common::LineageTreeAndIdMap>& lineage_trees,
      const std::string& lineage_tree_name
   );

  private:
   /// Fills the built-in reference genomes table, which must still be empty.
   static void fillReferenceGenomesTable(
      const ReferenceGenomes& reference_genomes,
      Database& database
   );

   static void createLineageRelationTable(
      std::string_view column_name,
      const common::LineageTreeAndIdMap& lineage_tree,
      Database& database
   );
};
}  // namespace rhydb::initialize
