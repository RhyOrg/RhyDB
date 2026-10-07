#include "rhydb/config/preprocessing_config.h"

#include <filesystem>

#include "config/config_interface.h"
#include "rhydb/config/config_defaults.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

namespace {
using rhydb::config::ConfigKeyPath;
using rhydb::config::YamlFile;

// Using functions instead of global variables because of
// initialization order issues.

ConfigKeyPath preprocessingConfigOptionKey() {
   return YamlFile::stringToConfigKeyPath("preprocessingConfig");
}
ConfigKeyPath defaultPreprocessingConfigOptionKey() {
   return YamlFile::stringToConfigKeyPath("defaultPreprocessingConfig");
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
ConfigKeyPath ndjsonInputFilenameOptionKey() {
   return YamlFile::stringToConfigKeyPath("ndjsonInputFilename");
}
ConfigKeyPath withoutUnalignedSequencesOptionKey() {
   return YamlFile::stringToConfigKeyPath("withoutUnalignedSequences");
}
}  // namespace

namespace rhydb::config {

// Specification of the fields in inputs to the PreprocessingConfig struct
ConfigSpecification PreprocessingConfig::getConfigSpecification() {
   return ConfigSpecification{
      .program_name = "rhydb preprocessing",
      .attribute_specifications{
         ConfigAttributeSpecification::createWithoutDefault(
            preprocessingConfigOptionKey(),
            ConfigValueType::PATH,
            "The path to a preprocessing config that should be read before overwriting\n"
            "its values with environment variables and other CLI arguments."
         ),
         ConfigAttributeSpecification::createWithoutDefault(
            defaultPreprocessingConfigOptionKey(),
            ConfigValueType::PATH,
            "The path to a default preprocessing config that should be read first.\n"
            "This path will often be set by an environment variable, thus \n"
            "providing defaults to a RhyDB instance in a specific environment (e.g. Docker)."
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
         ConfigAttributeSpecification::createWithoutDefault(
            ndjsonInputFilenameOptionKey(),
            ConfigValueType::PATH,
            "Path to the input data. Relative from inputDirectory."
         ),
         ConfigAttributeSpecification::createWithDefault(
            withoutUnalignedSequencesOptionKey(),
            ConfigValue::fromBool(false),
            "Whether unaligned sequences should be omitted for each aligned nucleotide sequence."
         ),
      },
   };
}

PreprocessingConfig PreprocessingConfig::withDefaults() {
   PreprocessingConfig result;
   result.overwriteFrom(getConfigSpecification().getConfigSourceFromDefaults());
   return result;
}

void PreprocessingConfig::validate() const {
   if (!input_file.has_value()) {
      throw rhydb::preprocessing::PreprocessingException(
         "'ndjsonInputFilename' must be specified as preprocessing option."
      );
   }
}

void PreprocessingConfig::overwriteFrom(const VerifiedConfigAttributes& config_source) {
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
   if (auto var = config_source.getBool(withoutUnalignedSequencesOptionKey())) {
      initialization_files.without_unaligned_sequences = var.value();
   }
   if (auto var = config_source.getPath(outputDirectoryOptionKey())) {
      output_directory = var.value();
   }
   if (auto var = config_source.getPath(ndjsonInputFilenameOptionKey())) {
      input_file = var.value();
   }
}

std::vector<std::filesystem::path> PreprocessingConfig::getConfigFilePaths(
   const VerifiedCommandLineArguments& cmd_source,
   const VerifiedConfigAttributes& env_source
) {
   std::vector<std::filesystem::path> result;
   auto default_runtime_config =
      getConfigFilePath(defaultPreprocessingConfigOptionKey(), cmd_source, env_source);
   if (default_runtime_config.has_value()) {
      result.emplace_back(default_runtime_config.value());
   }
   auto runtime_config = getConfigFilePath(preprocessingConfigOptionKey(), cmd_source, env_source);
   if (runtime_config.has_value()) {
      result.emplace_back(runtime_config.value());
   }
   return result;
}
std::optional<std::filesystem::path> PreprocessingConfig::getInputFilePath() const {
   if (input_file.has_value()) {
      return initialization_files.directory / input_file.value();
   }
   return std::nullopt;
}

}  // namespace rhydb::config

[[maybe_unused]] auto fmt::formatter<rhydb::config::PreprocessingConfig>::format(
   const rhydb::config::PreprocessingConfig& preprocessing_config,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   const nlohmann::json json = preprocessing_config;
   return fmt::format_to(ctx.out(), "{}", json.dump());
}
