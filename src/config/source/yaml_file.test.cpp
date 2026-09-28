#include "config/source/yaml_file.h"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "config/config_key_path.h"
#include "rhydb/common/fmt_formatters.h"

using rhydb::config::ConfigKeyPath;
using rhydb::config::YamlFile;

TEST(YamlFile, simpleStringToConfigKeyPath) {
   auto under_test = YamlFile::stringToConfigKeyPath("test");
   ASSERT_EQ(under_test, (ConfigKeyPath::tryFrom({{{"test"}}})));
}

TEST(YamlFile, stringToConfigKeyPathWithDot) {
   auto under_test = YamlFile::stringToConfigKeyPath("api.port");
   ASSERT_EQ(under_test, (ConfigKeyPath::tryFrom({{"api"}, {"port"}})));
   ASSERT_NE(under_test, (ConfigKeyPath::tryFrom({{"api", "port"}})));
}

TEST(YamlFile, stringToConfigKeyPathWithCamelCase) {
   auto under_test = YamlFile::stringToConfigKeyPath("query.materializationCutoff");
   ASSERT_EQ(under_test, (ConfigKeyPath::tryFrom({{"query"}, {"materialization", "cutoff"}})));
}

TEST(YamlFile, errorOnPascalCase) {
   EXPECT_THAT(
      []() { YamlFile::stringToConfigKeyPath("Api.Port"); },
      ThrowsMessage<std::runtime_error>(::testing::HasSubstr("'Api.Port' is not a valid YamlPath"))
   );
}

TEST(YamlFile, configKeyPathToString) {
   ASSERT_EQ(YamlFile::configKeyPathToString(ConfigKeyPath::tryFrom({{"test"}}).value()), "test");
   ASSERT_EQ(
      YamlFile::configKeyPathToString(
         (ConfigKeyPath::tryFrom({{"query"}, {"materialization", "cutoff"}})).value()
      ),
      "query.materializationCutoff"
   );
}

TEST(YamlFile, validRoundTrip) {
   auto under_test =
      std::vector<std::string>{"test", "somethingElse.that.is.quiteLong", "a.2.3.4", "asd", "aa"};
   for (const auto& string : under_test) {
      ASSERT_EQ(YamlFile::configKeyPathToString(YamlFile::stringToConfigKeyPath(string)), string);
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(YamlFile, containsCorrectFieldsFromFlatYAML) {
   const auto under_test = YamlFile::fromYAML(
                              "inline",
                              R"(
inputDirectory: "./testBaseData/exampleDataset/"
outputDirectory: "./output/"
ndjsonInputFilename: "input_file.ndjson"
lineageDefinitionFilenames: "lineage_definition.yaml"
phyloTreeFilename: "phylogenetic_tree.yaml"
referenceGenomeFilename: "reference_genomes.json"
)"
   )
                              .getYamlFields();

   const std::unordered_map<ConfigKeyPath, YAML::Node> expected_result{
      {YamlFile::stringToConfigKeyPath("inputDirectory"),
       YAML::Node{"./testBaseData/exampleDataset/"},},
      {YamlFile::stringToConfigKeyPath("outputDirectory"), YAML::Node{"./output/"}},
      {YamlFile::stringToConfigKeyPath("ndjsonInputFilename"), YAML::Node{"input_file.ndjson"}},
      {YamlFile::stringToConfigKeyPath("lineageDefinitionFilenames"),
       YAML::Node{"lineage_definition.yaml"},},
      {YamlFile::stringToConfigKeyPath("phyloTreeFilename"), YAML::Node{"phylogenetic_tree.yaml"}},
      {YamlFile::stringToConfigKeyPath("referenceGenomeFilename"),
       YAML::Node{"reference_genomes.json"},},
   };

   for (const auto& [key, value] : expected_result) {
      ASSERT_TRUE(under_test.contains(key));
      ASSERT_EQ(under_test.at(key).as<std::string>(), value.as<std::string>());
   }
   for (const auto& [key, value] : under_test) {
      ASSERT_TRUE(expected_result.contains(key));
      ASSERT_EQ(expected_result.at(key).as<std::string>(), value.as<std::string>());
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(YamlFile, containsCorrectFieldsFromNestedYAML) {
   const auto under_test = YamlFile::fromYAML("inline", R"(
dataDirectory: "test/directory"
api:
   port: 1234)")
                              .getYamlFields();

   const std::unordered_map<ConfigKeyPath, YAML::Node> expected_result{
      {YamlFile::stringToConfigKeyPath("dataDirectory"), YAML::Node{"test/directory"}},
      {YamlFile::stringToConfigKeyPath("api.port"), YAML::Node{1234}},
   };

   for (const auto& [key, value] : expected_result) {
      ASSERT_TRUE(under_test.contains(key));
      ASSERT_EQ(under_test.at(key).as<std::string>(), value.as<std::string>());
   }
   for (const auto& [key, value] : under_test) {
      ASSERT_TRUE(expected_result.contains(key));
      ASSERT_EQ(expected_result.at(key).as<std::string>(), value.as<std::string>());
   }
}

TEST(YamlFile, shouldThrowExceptionWhenConfigFileDoesNotExist) {
   EXPECT_THAT(
      []() { YamlFile::readFile("testBaseData/does_not_exist.yaml"); },
      ThrowsMessage<std::runtime_error>(
         ::testing::HasSubstr("Could not open the YAML file: 'testBaseData/does_not_exist.yaml'")
      )
   );
}

TEST(YamlFile, shouldThrowExceptionWhenConfigFileCannotBeParsed) {
   EXPECT_THAT(
      []() {
         YamlFile::fromYAML("string", R"(
X
s:
)");
      },
      ThrowsMessage<std::runtime_error>(
         ::testing::HasSubstr("string does not contain valid YAML: yaml-cpp: error at line 3, "
                              "column 2: illegal map value")
      )
   );
}
