#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "rhydb/config/initialize_config.h"
#include "rhydb/database.h"
#include "rhydb/preprocessing/preprocessing.h"

/// Runs the preprocessing of the `preprocessing` command on `database`, with the initialization
/// files given as paths rather than relative to an input directory, which Cython cannot easily
/// construct.
inline void preprocessDatabase(
   rhydb::Database& database,
   const std::string& database_config_file,
   const std::string& reference_genome_file,
   const std::string& input_file,
   const std::vector<std::string>& lineage_definition_files,
   const std::optional<std::string>& phylogenetic_tree_file,
   bool without_unaligned_sequences
) {
   rhydb::config::InitializationFiles initialization_files;
   initialization_files.database_config_file = database_config_file;
   initialization_files.reference_genome_file = reference_genome_file;
   initialization_files.lineage_definition_files = lineage_definition_files;
   if (phylogenetic_tree_file.has_value()) {
      initialization_files.phylogenetic_tree_file = phylogenetic_tree_file.value();
   }
   initialization_files.without_unaligned_sequences = without_unaligned_sequences;
   rhydb::preprocessing::preprocessing(
      database, initialization_files, std::filesystem::path{input_file}
   );
}
