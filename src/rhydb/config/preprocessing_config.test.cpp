#include "rhydb/config/preprocessing_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "config/source/yaml_file.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

using rhydb::config::PreprocessingConfig;
using rhydb::config::YamlFile;

TEST(PreprocessingConfig, shouldReadConfigWithCorrectParametersAndDefaults) {
   auto config = PreprocessingConfig::withDefaults();

   config.overwriteFrom(
      YamlFile::readFile("./testBaseData/test_preprocessing_config.yaml")
         .verify(PreprocessingConfig::getConfigSpecification())
   );

   const std::filesystem::path input_directory = "./testBaseData/exampleDataset/";
   ASSERT_TRUE(config.input_file.has_value());
   ASSERT_EQ(config.getInputFilePath(), input_directory / "input_file.ndjson");
   ASSERT_EQ(
      config.initialization_files.getLineageDefinitionFilepaths(),
      (std::vector<std::filesystem::path>{input_directory / "lineage_definition.yaml"})
   );
   ASSERT_EQ(
      config.initialization_files.getPhyloTreeFilepath(), input_directory / "phylogenetic_tree.nwk"
   );
}

TEST(PreprocessingConfig, shouldReadConfigWithOverriddenDefaults) {
   auto config = PreprocessingConfig::withDefaults();

   config.overwriteFrom(
      YamlFile::fromYAML("inline", R"(
inputDirectory: "./testBaseData/exampleDataset/"
outputDirectory: "./output/custom/"
ndjsonInputFilename: "input_file.ndjson"
lineageDefinitionFilenames:
  - "lineage_definition.yaml"
phyloTreeFilename: "phylogenetic_tree.yaml"
referenceGenomeFilename: "reference_genomes.json")")
         .verify(PreprocessingConfig::getConfigSpecification())
   );

   const std::filesystem::path input_directory = "./testBaseData/exampleDataset/";
   ASSERT_TRUE(config.input_file.has_value());
   ASSERT_EQ(config.getInputFilePath(), input_directory / "input_file.ndjson");
   ASSERT_EQ(
      config.initialization_files.getLineageDefinitionFilepaths(),
      std::vector<std::filesystem::path>{input_directory / "lineage_definition.yaml"}
   );
   ASSERT_EQ(
      config.initialization_files.getPhyloTreeFilepath(), input_directory / "phylogenetic_tree.yaml"
   );

   ASSERT_EQ(config.output_directory, "./output/custom/");
}

TEST(PreprocessingConfig, shouldThrowErrorWhenNdjsonInputFileNameIsNotSet) {
   auto config = PreprocessingConfig::withDefaults();
   EXPECT_THAT(
      [&config]() { config.validate(); },
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(
         ::testing::HasSubstr("'ndjsonInputFilename' must be specified as preprocessing option.")
      )
   );
}
