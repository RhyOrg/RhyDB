#include "rhydb/config/initialize_config.h"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "config/config_interface.h"
#include "rhydb/config/config_defaults.h"

namespace {
using rhydb::config::ConfigKeyPath;
using rhydb::config::YamlFile;

// Using functions instead of global variables because of
// initialization order issues.

ConfigKeyPath initializeConfigOptionKey() {
   return YamlFile::stringToConfigKeyPath("initializeConfig");
}
ConfigKeyPath inputDirectoryOptionKey() {
   return YamlFile::stringToConfigKeyPath("inputDirectory");
}
ConfigKeyPath outputDirectoryOptionKey() {
   return YamlFile::stringToConfigKeyPath("outputDirectory");
}
ConfigKeyPath lineageDefinitionFilenamesOptionKey() {
   return YamlFile::stringToConfigKeyPath("lineageDefinitionFilenames");
}
ConfigKeyPath phyloTreeFilenameOptionKey() {
   return YamlFile::stringToConfigKeyPath("phyloTreeFilename");
}
ConfigKeyPath databaseConfigFileOptionKey() {
   return YamlFile::stringToConfigKeyPath("databaseConfig");
}
ConfigKeyPath referenceGenomeFilenameOptionKey() {
   return YamlFile::stringToConfigKeyPath("referenceGenomeFilename");
}
ConfigKeyPath withoutUnalignedSequencesOptionKey() {
   return YamlFile::stringToConfigKeyPath("withoutUnalignedSequences");
}
}  // namespace

namespace rhydb::config {

// Specification of the fields in inputs to the PreprocessingConfig struct
ConfigSpecification InitializeConfig::getConfigSpecification() {
   return ConfigSpecification{
      .program_name = "rhydb initialize",
      .attribute_specifications{
         ConfigAttributeSpecification::createWithoutDefault(
            initializeConfigOptionKey(),
            ConfigValueType::PATH,
            "The path to an initialize config that should be read before overwriting\n"
            "its values with environment variables and other CLI arguments."
         ),
         ConfigAttributeSpecification::createWithDefault(
            inputDirectoryOptionKey(),
            ConfigValue::fromPath("./"),
            "The path to the directory with the input files."
         ),
         ConfigAttributeSpecification::createWithDefault(
            outputDirectoryOptionKey(),
            ConfigValue::fromPath(DEFAULT_OUTPUT_DIRECTORY),
            "The path to the directory to hold the output files."
         ),
         ConfigAttributeSpecification::createWithoutDefault(
            lineageDefinitionFilenamesOptionKey(),
            ConfigValueType::LIST,
            "List of file names holding the lineage definitions. Relative from inputDirectory."
         ),
         ConfigAttributeSpecification::createWithoutDefault(
            phyloTreeFilenameOptionKey(),
            ConfigValueType::PATH,
            "File name of the file holding the phylogenetic tree. Relative from inputDirectory."
         ),
         ConfigAttributeSpecification::createWithDefault(
            databaseConfigFileOptionKey(),
            ConfigValue::fromPath("database_config.yaml"),
            "File name of the file holding the database table configuration. Relative from "
            "inputDirectory."
         ),
         ConfigAttributeSpecification::createWithDefault(
            referenceGenomeFilenameOptionKey(),
            ConfigValue::fromPath("reference_genomes.json"),
            "File name of the file holding the reference genome. Relative from inputDirectory."
         ),
         ConfigAttributeSpecification::createWithDefault(
            withoutUnalignedSequencesOptionKey(),
            ConfigValue::fromBool(false),
            "Whether unaligned sequences should be omitted for each aligned nucleotide sequence."
         ),
      },
   };
}

InitializeConfig InitializeConfig::withDefaults() {
   InitializeConfig result;
   result.overwriteFrom(getConfigSpecification().getConfigSourceFromDefaults());
   return result;
}

void InitializeConfig::validate() const {}

std::filesystem::path InitializationFiles::getDatabaseConfigFilepath() const {
   return directory / database_config_file;
}

std::vector<std::filesystem::path> InitializationFiles::getLineageDefinitionFilepaths() const {
   std::vector<std::filesystem::path> paths;
   paths.reserve(lineage_definition_files.size());
   for (const auto& file_name : lineage_definition_files) {
      paths.push_back(directory / file_name);
   }
   return paths;
}

std::optional<std::filesystem::path> InitializationFiles::getPhyloTreeFilepath() const {
   return phylogenetic_tree_file.has_value()
             ? std::optional(directory / phylogenetic_tree_file.value())
             : std::nullopt;
}

std::filesystem::path InitializationFiles::getReferenceGenomeFilepath() const {
   return directory / reference_genome_file;
}

void InitializeConfig::overwriteFrom(const VerifiedConfigAttributes& config_source) {
   if (auto var = config_source.getPath(inputDirectoryOptionKey())) {
      initialization_files.directory = var.value();
   }
   if (auto var = config_source.getList(lineageDefinitionFilenamesOptionKey())) {
      initialization_files.lineage_definition_files = var.value();
   }
   if (auto var = config_source.getPath(phyloTreeFilenameOptionKey())) {
      initialization_files.phylogenetic_tree_file = var.value();
   }
   if (auto var = config_source.getPath(databaseConfigFileOptionKey())) {
      initialization_files.database_config_file = var.value();
   }
   if (auto var = config_source.getPath(referenceGenomeFilenameOptionKey())) {
      initialization_files.reference_genome_file = var.value();
   }
   if (auto var = config_source.getPath(outputDirectoryOptionKey())) {
      output_directory = var.value();
   }
   if (auto var = config_source.getBool(withoutUnalignedSequencesOptionKey())) {
      initialization_files.without_unaligned_sequences = var.value();
   }
}

std::vector<std::filesystem::path> InitializeConfig::getConfigFilePaths(
   const VerifiedCommandLineArguments& cmd_source,
   const VerifiedConfigAttributes& env_source
) {
   std::vector<std::filesystem::path> result;
   auto runtime_config = getConfigFilePath(initializeConfigOptionKey(), cmd_source, env_source);
   if (runtime_config.has_value()) {
      result.emplace_back(runtime_config.value());
   }
   return result;
}

}  // namespace rhydb::config

[[maybe_unused]] auto fmt::formatter<rhydb::config::InitializeConfig>::format(
   const rhydb::config::InitializeConfig& initialize_config,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   const nlohmann::json json = initialize_config;
   return fmt::format_to(ctx.out(), "{}", json.dump());
}
