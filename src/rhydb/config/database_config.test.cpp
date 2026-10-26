#include "rhydb/config/database_config.h"

#include <fmt/format.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "config/config_exception.h"

using rhydb::config::ConfigException;
using rhydb::config::DatabaseConfig;
using rhydb::config::toDatabaseDataType;
using rhydb::schema::DataType;

TEST(DatabaseMetadataType, shouldBeConvertableFromString) {
   ASSERT_TRUE(toDatabaseDataType("string") == DataType::STRING);
   ASSERT_TRUE(toDatabaseDataType("date") == DataType::DATE32);
   ASSERT_TRUE(toDatabaseDataType("int") == DataType::INT32);
   ASSERT_TRUE(toDatabaseDataType("int32") == DataType::INT32);
   ASSERT_TRUE(toDatabaseDataType("int64") == DataType::INT64);
   ASSERT_THROW(toDatabaseDataType("unknown"), ConfigException);
}

TEST(DatabaseMetadataType, shouldBeWrittenWithItsCanonicalName) {
   const DatabaseConfig config = DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: test
  metadata:
    - name: key
      type: string
    - name: age
      type: int
  primaryKey: key
)");
   const YAML::Node node =
      YAML::convert<rhydb::config::DatabaseMetadata>::encode(config.schema.metadata.at(1));
   EXPECT_EQ(node["type"].as<std::string>(), "int32");
}

TEST(DatabaseConfig, shouldBuildDatabaseConfig) {
   const DatabaseConfig config = DatabaseConfig::getValidatedConfig(
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "metadata1"
      type: "string"
    - name: "metadata2"
      type: "string"
    - name: "testPrimaryKey"
      type: "date"
  primaryKey: "testPrimaryKey"
)"
   );
   ASSERT_TRUE(config.schema.instance_name == "testInstanceName");
   ASSERT_TRUE(config.schema.primary_key == "testPrimaryKey");
   ASSERT_TRUE(config.schema.metadata[0].name == "metadata1");
   ASSERT_TRUE(config.schema.metadata.size() == 3);
}

TEST(DatabaseConfig, shouldBuildDatabaseConfigWithoutPrimaryKeyWhenKeyIsOmitted) {
   const DatabaseConfig config = DatabaseConfig::getValidatedConfig(
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "metadata1"
      type: "string"
)"
   );
   ASSERT_FALSE(config.schema.primary_key.has_value());
}

TEST(DatabaseConfig, shouldBuildDatabaseConfigWithoutPrimaryKeyWhenValueIsNull) {
   for (const auto* const primary_key_line : {"primaryKey: null", "primaryKey: ~", "primaryKey:"}) {
      const DatabaseConfig config = DatabaseConfig::getValidatedConfig(
         fmt::format(
            R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "metadata1"
      type: "string"
  {}
)",
            primary_key_line
         )
      );
      ASSERT_FALSE(config.schema.primary_key.has_value()) << primary_key_line;
   }
}

namespace {

struct TestParameter {
   DataType value_type;
   bool generate_index;
   bool generate_lineage_index;
   bool phylo_tree_node_identifier;
   DataType expected_column_type;
};

class DatabaseMetadataFixture : public ::testing::TestWithParam<TestParameter> {};

TEST_P(DatabaseMetadataFixture, getColumnTypeShouldReturnCorrectColumnType) {
   const auto& test_parameter = GetParam();

   const rhydb::config::DatabaseMetadata under_test = {
      .name = "testName",
      .type = test_parameter.value_type,
      .generate_index = test_parameter.generate_index,
      .phylo_tree_node_identifier = test_parameter.phylo_tree_node_identifier,
   };

   ASSERT_EQ(under_test.getColumnType(), test_parameter.expected_column_type);
}

INSTANTIATE_TEST_SUITE_P(
   DatabaseMetadata,
   DatabaseMetadataFixture,
   ::testing::Values(
      TestParameter{
         .value_type = DataType::STRING,
         .generate_index = false,
         .phylo_tree_node_identifier = true,
         .expected_column_type = DataType::STRING,
      },
      TestParameter{
         .value_type = DataType::STRING,
         .generate_index = true,
         .expected_column_type = DataType::dictionaryEncoded(DataType::STRING),
      },
      TestParameter{
         .value_type = DataType::DATE32,
         .generate_index = false,
         .expected_column_type = DataType::DATE32,
      },
      TestParameter{
         .value_type = DataType::STRING,
         .generate_index = true,
         .generate_lineage_index = true,
         .expected_column_type = DataType::dictionaryEncoded(DataType::STRING),
      },
      TestParameter{
         .value_type = DataType::INT32,
         .generate_index = false,
         .expected_column_type = DataType::INT32,
      },
      TestParameter{
         .value_type = DataType::INT64,
         .generate_index = false,
         .expected_column_type = DataType::INT64,
      },
      TestParameter{
         .value_type = DataType::FLOAT,
         .generate_index = false,
         .expected_column_type = DataType::FLOAT,
      }
   )
);

TEST(DatabaseConfig, shouldReadConfigWithCorrectParameters) {
   DatabaseConfig config =
      DatabaseConfig::getValidatedConfigFromFile("testBaseData/test_database_config.yaml");

   ASSERT_EQ(config.schema.instance_name, "sars_cov-2_minimal_test_config");
   ASSERT_EQ(config.schema.primary_key, "primary_key");
   ASSERT_EQ(config.schema.metadata.size(), 11);
   ASSERT_EQ(config.schema.metadata[0].name, "primary_key");
   ASSERT_EQ(config.schema.metadata[0].type, DataType::STRING);
   ASSERT_EQ(config.schema.metadata[0].generate_index, false);
   ASSERT_EQ(config.schema.metadata[0].generate_lineage_index, std::nullopt);
   ASSERT_EQ(config.schema.metadata[0].phylo_tree_node_identifier, true);
   ASSERT_EQ(config.schema.metadata[0].generate_index, false);
   ASSERT_EQ(config.schema.metadata[1].name, "date");
   ASSERT_EQ(config.schema.metadata[1].type, DataType::DATE32);
   ASSERT_EQ(config.schema.metadata[1].generate_index, false);
   ASSERT_EQ(config.schema.metadata[2].name, "unsorted_date");
   ASSERT_EQ(config.schema.metadata[2].type, DataType::DATE32);
   ASSERT_EQ(config.schema.metadata[2].generate_index, false);
   ASSERT_EQ(config.schema.metadata[3].name, "region");
   ASSERT_EQ(config.schema.metadata[3].type, DataType::STRING);
   ASSERT_EQ(config.schema.metadata[3].generate_index, true);
   ASSERT_EQ(config.schema.metadata[4].name, "country");
   ASSERT_EQ(config.schema.metadata[4].type, DataType::STRING);
   ASSERT_EQ(config.schema.metadata[4].generate_index, true);
   ASSERT_EQ(config.schema.metadata[5].name, "pango_lineage");
   ASSERT_EQ(config.schema.metadata[5].type, DataType::STRING);
   ASSERT_EQ(config.schema.metadata[5].generate_index, true);
   ASSERT_EQ(config.schema.metadata[5].generate_lineage_index, "some_test_value");
   ASSERT_EQ(config.schema.metadata[6].name, "division");
   ASSERT_EQ(config.schema.metadata[6].type, DataType::STRING);
   ASSERT_EQ(config.schema.metadata[6].generate_index, true);
   ASSERT_EQ(config.schema.metadata[6].generate_lineage_index, std::nullopt);
   ASSERT_EQ(config.schema.metadata[7].name, "age");
   ASSERT_EQ(config.schema.metadata[7].type, DataType::INT32);
   ASSERT_EQ(config.schema.metadata[7].generate_index, false);
   ASSERT_EQ(config.schema.metadata[8].name, "age_32");
   ASSERT_EQ(config.schema.metadata[8].type, DataType::INT32);
   ASSERT_EQ(config.schema.metadata[8].generate_index, false);
   ASSERT_EQ(config.schema.metadata[9].name, "age_64");
   ASSERT_EQ(config.schema.metadata[9].type, DataType::INT64);
   ASSERT_EQ(config.schema.metadata[9].generate_index, false);
   ASSERT_EQ(config.schema.metadata[10].name, "qc_value");
   ASSERT_EQ(config.schema.metadata[10].type, DataType::FLOAT);
   ASSERT_EQ(config.schema.metadata[10].generate_index, false);
}

TEST(DatabaseConfig, shouldThrowExceptionWhenConfigFileDoesNotExist) {
   EXPECT_THAT(
      []() {
         (void)DatabaseConfig::getValidatedConfigFromFile("testBaseData/does_not_exist.yaml");
      },
      ThrowsMessage<std::runtime_error>(::testing::HasSubstr("Failed to read database config"))
   );
}

TEST(DatabaseConfig, shouldThrowErrorForInvalidMetadataType) {
   const auto* yaml = R"-(
schema:
  instanceName: dummy name
  metadata:
    - name: wrongType
      type: wrong_type
  primaryKey: primary_key
)-";

   ASSERT_THROW((void)DatabaseConfig::getValidatedConfig(yaml), ConfigException);
}

TEST(DatabaseConfig, shouldNotThrowIfThereAreAdditionalEntries) {
   const auto* yaml = R"-(
schema:
  instanceName: dummy name
  metadata:
    - name: key
      type: string
  primaryKey: key
  features:
    - name: this is unknown to RhyDB
)-";

   ASSERT_NO_THROW((void)DatabaseConfig::getValidatedConfig(yaml));
}

TEST(DatabaseConfig, shouldThrowIfTheConfigHasAnInvalidStructure) {
   const auto* yaml = R"-(
schema:
  instanceName: dummy name
  primaryKey: missing metadata
)-";

   EXPECT_THAT(
      [yaml]() { (void)DatabaseConfig::getValidatedConfig(yaml); },
      ThrowsMessage<std::runtime_error>(
         ::testing::HasSubstr("invalid node; first invalid key: \"metadata\"")
      )
   );
}

TEST(DatabaseConfig, shouldReadConfigWithoutErrors) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "testPrimaryKey"
      type: "string"
    - name: "metadata1"
      type: "string"
      generateIndex: true
      generateLineageIndex: lineage
    - name: "metadata2"
      type: "date"
    - name: "metadata3"
      type: "string"
      isPhyloTreeField: true
  primaryKey: "testPrimaryKey"
)";

   ASSERT_NO_THROW(DatabaseConfig::getValidatedConfig(config_yaml));
}

TEST(DatabaseConfig, shouldThrowIfPrimaryKeyIsNotInMetadata) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "notPrimaryKey"
      type: "string"
  primaryKey: "testPrimaryKey"
)";

   ASSERT_THROW(DatabaseConfig::getValidatedConfig(config_yaml), ConfigException);
}

TEST(DatabaseConfig, shouldThrowIfThereAreTwoMetadataWithTheSameName) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "testPrimaryKey"
      type: "string"
    - name: "sameName"
      type: "string"
    - name: "sameName"
      type: "date"
  primaryKey: "testPrimaryKey"
)";

   ASSERT_THROW(DatabaseConfig::getValidatedConfig(config_yaml), ConfigException);
}

TEST(DatabaseConfig, givenMetadataToGenerateIndexForThatIsNotStringOrDateThenThrows) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "testPrimaryKey"
      type: "string"
    - name: "indexed int"
      type: "int"
      generateIndex: true
  primaryKey: "testPrimaryKey"
)";

   EXPECT_THAT(
      [&config_yaml]() { DatabaseConfig::getValidatedConfig(config_yaml); },
      ThrowsMessage<ConfigException>(::testing::HasSubstr(
         "Metadata 'indexed int' generateIndex is set, but generating an "
         "index is only allowed for types STRING and DATE"
      ))
   );
}

TEST(DatabaseConfig, givenMetadataToGenerateIndexForDateThenItIsDictionaryEncoded) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "testPrimaryKey"
      type: "string"
    - name: "indexed date"
      type: "date"
      generateIndex: true
  primaryKey: "testPrimaryKey"
)";

   const auto config = DatabaseConfig::getValidatedConfig(config_yaml);
   const auto metadata = config.getMetadata("indexed date");
   ASSERT_TRUE(metadata.has_value());
   EXPECT_EQ(metadata->getColumnType(), DataType::dictionaryEncoded(DataType::DATE32));
}

TEST(DatabaseConfig, givenLineageIndexAndNotGenerateThenThrows) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "testPrimaryKey"
      type: "string"
    - name: "some lineage"
      type: "string"
      generateLineageIndex: lineage
  primaryKey: "testPrimaryKey"
)";

   EXPECT_THAT(
      [&config_yaml]() { DatabaseConfig::getValidatedConfig(config_yaml); },
      ThrowsMessage<ConfigException>(::testing::HasSubstr(
         "Metadata 'some lineage' generateLineageIndex is set, "
         "generateIndex must also be set"
      ))
   );
}

TEST(DatabaseConfig, givenPhyloTreeIndexAndGenerateThenThrows) {
   const char* const config_yaml =
      R"(
schema:
  instanceName: "testInstanceName"
  metadata:
    - name: "testPrimaryKey"
      type: "string"
    - name: "some lineage"
      type: "string"
      isPhyloTreeField: true
      generateIndex: true
  primaryKey: "testPrimaryKey"
)";

   EXPECT_THAT(
      [&config_yaml]() { DatabaseConfig::getValidatedConfig(config_yaml); },
      ThrowsMessage<ConfigException>(::testing::HasSubstr(
         "Metadata 'some lineage' isPhyloTreeField and generateIndex "
         "are both set, if isPhyloTreeField is "
         "set then generateIndex cannot be set."
      ))
   );
}

}  // namespace
