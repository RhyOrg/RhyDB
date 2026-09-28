#include "rhydb/initialize/initializer.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

using rhydb::ReferenceGenomes;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::common::PhyloTree;
using rhydb::initialize::Initializer;

TEST(Initializer, correctlyCreatesSchemaFromInitializationFiles) {
   const rhydb::config::DatabaseConfig database_config =
      rhydb::config::DatabaseConfig::getValidatedConfigFromFile(
         "testBaseData/unitTestDummyDataset/database_config.yaml"
      );
   const ReferenceGenomes reference_genomes =
      ReferenceGenomes::readFromFile("testBaseData/unitTestDummyDataset/reference_genomes.json");
   const PhyloTree phylo_tree_file =
      PhyloTree::fromNewickFile("testBaseData/unitTestDummyDataset/phylogenetic_tree.nwk");
   const std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees{
      {"test_lineage_definition.yaml",
       LineageTreeAndIdMap::fromLineageDefinitionFile(
          rhydb::preprocessing::LineageDefinitionFile::fromYAMLString(R"(
A:
  aliases:
  - X
A.1:
  parents:
  - A
A.11:
  aliases:
  - X2
  - Y3
  parents:
  - A
)")
       ),},
   };
   auto table_schema = Initializer::createSchemaFromConfigFiles(
      database_config,
      reference_genomes,
      lineage_trees,
      phylo_tree_file,
      /*without_unaligned_sequences=*/false
   );

   const size_t expected_number_of_columns =
      database_config.schema.metadata.size() + reference_genomes.aa_sequence_names.size() +
      (reference_genomes.nucleotide_sequence_names.size() * 2);
   ASSERT_EQ(table_schema->getColumnIdentifiers().size(), expected_number_of_columns);
   ASSERT_TRUE(table_schema->primary_key.has_value());
   ASSERT_EQ(table_schema->primary_key->name, database_config.schema.primary_key.value());

   using rhydb::schema::ColumnType;
   ASSERT_TRUE(table_schema->getColumn("M").has_value());
   ASSERT_EQ(table_schema->getColumn("M").value().type, ColumnType::AMINO_ACID_SEQUENCE);
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<rhydb::storage::column::SequenceColumn<rhydb::AminoAcid>>("M")
                  .has_value());
   ASSERT_EQ(
      table_schema->getColumnMetadata<rhydb::storage::column::SequenceColumn<rhydb::AminoAcid>>("M")
         .value()
         ->reference_sequence,
      reference_genomes.stringToVector<rhydb::AminoAcid>("MADS*")
   );

   ASSERT_TRUE(table_schema->getColumn("age").has_value());
   ASSERT_EQ(table_schema->getColumn("age").value().type, ColumnType::INT32);

   ASSERT_TRUE(table_schema->getColumn("country").has_value());
   ASSERT_EQ(table_schema->getColumn("country").value().type, ColumnType::DICTIONARY_ENCODED);
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<rhydb::storage::column::DictionaryEncodedColumn>("country")
                  .has_value());

   ASSERT_TRUE(table_schema->getColumn("date").has_value());
   ASSERT_EQ(table_schema->getColumn("date").value().type, ColumnType::DATE32);

   ASSERT_TRUE(table_schema->getColumn("division").has_value());
   ASSERT_EQ(table_schema->getColumn("division").value().type, ColumnType::DICTIONARY_ENCODED);
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<rhydb::storage::column::DictionaryEncodedColumn>("division")
                  .has_value());

   ASSERT_TRUE(table_schema->getColumn("main").has_value());
   ASSERT_EQ(table_schema->getColumn("main").value().type, ColumnType::NUCLEOTIDE_SEQUENCE);
   ASSERT_TRUE(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::SequenceColumn<rhydb::Nucleotide>>("main")
         .has_value()
   );
   ASSERT_EQ(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::SequenceColumn<rhydb::Nucleotide>>("main")
         .value()
         ->reference_sequence,
      reference_genomes.stringToVector<rhydb::Nucleotide>("ACGTACGT")
   );

   ASSERT_TRUE(table_schema->getColumn("pango_lineage").has_value());
   ASSERT_EQ(table_schema->getColumn("pango_lineage").value().type, ColumnType::DICTIONARY_ENCODED);
   ASSERT_TRUE(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::DictionaryEncodedColumn>("pango_lineage")
         .has_value()
   );
   auto* pango_metadata =
      table_schema
         ->getColumnMetadata<rhydb::storage::column::DictionaryEncodedColumn>("pango_lineage")
         .value();
   ASSERT_EQ(pango_metadata->dictionary.getValue(0), "A");
   ASSERT_EQ(pango_metadata->dictionary.getValue(1), "A.1");
   ASSERT_EQ(pango_metadata->dictionary.getValue(2), "A.11");
   ASSERT_EQ(pango_metadata->dictionary.getValue(3), "X");
   ASSERT_EQ(pango_metadata->dictionary.getValue(4), "X2");
   ASSERT_EQ(pango_metadata->dictionary.getValue(5), "Y3");

   ASSERT_TRUE(table_schema->getColumn("primaryKey").has_value());
   ASSERT_EQ(table_schema->getColumn("primaryKey").value().type, ColumnType::STRING);
   ASSERT_TRUE(table_schema->getColumnMetadata<rhydb::storage::column::StringColumn>("primaryKey")
                  .has_value());

   ASSERT_TRUE(table_schema->getColumn("qc_value").has_value());
   ASSERT_EQ(table_schema->getColumn("qc_value").value().type, ColumnType::FLOAT);

   ASSERT_TRUE(table_schema->getColumn("region").has_value());
   ASSERT_EQ(table_schema->getColumn("region").value().type, ColumnType::DICTIONARY_ENCODED);
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<rhydb::storage::column::DictionaryEncodedColumn>("region")
                  .has_value());

   ASSERT_TRUE(table_schema->getColumn("testSecondSequence").has_value());
   ASSERT_EQ(
      table_schema->getColumn("testSecondSequence").value().type, ColumnType::NUCLEOTIDE_SEQUENCE
   );
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<rhydb::storage::column::SequenceColumn<rhydb::Nucleotide>>(
                     "testSecondSequence"
                  )
                  .has_value());
   ASSERT_EQ(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::SequenceColumn<rhydb::Nucleotide>>(
            "testSecondSequence"
         )
         .value()
         ->reference_sequence,
      reference_genomes.stringToVector<rhydb::Nucleotide>("ACGT")
   );

   ASSERT_TRUE(table_schema->getColumn("test_boolean_column").has_value());
   ASSERT_EQ(table_schema->getColumn("test_boolean_column").value().type, ColumnType::BOOL);

   ASSERT_TRUE(table_schema->getColumn("unaligned_main").has_value());
   ASSERT_EQ(
      table_schema->getColumn("unaligned_main").value().type, ColumnType::ZSTD_COMPRESSED_STRING
   );
   ASSERT_TRUE(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::ZstdCompressedStringColumn>("unaligned_main")
         .has_value()
   );
   ASSERT_EQ(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::ZstdCompressedStringColumn>("unaligned_main")
         .value()
         ->dictionary_string,
      "ACGTACGT"
   );

   ASSERT_TRUE(table_schema->getColumn("unaligned_testSecondSequence").has_value());
   ASSERT_EQ(
      table_schema->getColumn("unaligned_testSecondSequence").value().type,
      ColumnType::ZSTD_COMPRESSED_STRING
   );
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<rhydb::storage::column::ZstdCompressedStringColumn>(
                     "unaligned_testSecondSequence"
                  )
                  .has_value());
   ASSERT_EQ(
      table_schema
         ->getColumnMetadata<rhydb::storage::column::ZstdCompressedStringColumn>(
            "unaligned_testSecondSequence"
         )
         .value()
         ->dictionary_string,
      "ACGT"
   );

   ASSERT_TRUE(table_schema->getColumn("unsorted_date").has_value());
   ASSERT_EQ(table_schema->getColumn("unsorted_date").value().type, ColumnType::DATE32);

   ASSERT_TRUE(table_schema->primary_key.has_value());
   ASSERT_EQ(table_schema->primary_key->name, "primaryKey");
   ASSERT_EQ(table_schema->primary_key->type, ColumnType::STRING);
}

namespace {
// Builds a schema from a config whose single lineage column uses the given `lineageIndexType`, and
// returns whether the resulting column carries the in-memory lineage tree.
bool lineageColumnHasInMemoryTree(const std::string& lineage_index_type) {
   const std::string config_yaml = fmt::format(
      R"(
schema:
  instanceName: "test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "lineage"
      type: "string"
      generateIndex: true
      generateLineageIndex: test_lineage_definition.yaml
      lineageIndexType: {}
  primaryKey: "primaryKey"
)",
      lineage_index_type
   );
   const auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(config_yaml);
   const ReferenceGenomes reference_genomes =
      ReferenceGenomes::readFromFile("testBaseData/unitTestDummyDataset/reference_genomes.json");
   const std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees{
      {"test_lineage_definition.yaml",
       LineageTreeAndIdMap::fromLineageDefinitionFile(
          rhydb::preprocessing::LineageDefinitionFile::fromYAMLString(R"(
A:
  parents: []
A.1:
  parents:
  - A
)")
       ),},
   };
   auto table_schema = Initializer::createSchemaFromConfigFiles(
      database_config, reference_genomes, lineage_trees, PhyloTree{}, false
   );
   auto* metadata =
      table_schema->getColumnMetadata<rhydb::storage::column::DictionaryEncodedColumn>("lineage")
         .value();
   return metadata->lineage_tree.has_value();
}
}  // namespace

TEST(Initializer, lineageIndexTypeColumnMetadataAttachesInMemoryTree) {
   EXPECT_TRUE(lineageColumnHasInMemoryTree("columnMetadata"));
}

TEST(Initializer, lineageIndexTypeBothAttachesInMemoryTree) {
   EXPECT_TRUE(lineageColumnHasInMemoryTree("both"));
}

TEST(Initializer, lineageIndexTypeTableDoesNotAttachInMemoryTree) {
   // 'table' mode materializes only the relation table; the column stays a plain indexed string
   // column without the in-memory lineage index.
   EXPECT_FALSE(lineageColumnHasInMemoryTree("table"));
}

class FindLineageTreeForName : public ::testing::Test {
  protected:
   void SetUp() override {
      // Set up test data
      test_lineage_tree1 = {};
      test_lineage_tree2 = LineageTreeAndIdMap::fromLineageDefinitionFile(
         rhydb::preprocessing::LineageDefinitionFile::fromYAMLString(R"(
some_lineage:
  parents:
    - some_parent
some_parent: ~)")
      );
      test_lineage_tree3 = LineageTreeAndIdMap::fromLineageDefinitionFile(
         rhydb::preprocessing::LineageDefinitionFile::fromYAMLString(R"(
some_other_lineage:
  parents:
    - some_parent
some_parent: ~)")
      );
   }

   LineageTreeAndIdMap test_lineage_tree1;
   LineageTreeAndIdMap test_lineage_tree2;
   LineageTreeAndIdMap test_lineage_tree3;
};

// Test finding with exact match (no prefix/suffix)
TEST_F(FindLineageTreeForName, FindLineageTree_ExactMatch) {
   std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;
   lineage_trees["test_tree"] = test_lineage_tree1;

   auto result = Initializer::findLineageTreeForName(lineage_trees, "test_tree");

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(result.value().file, test_lineage_tree1.file);
}

// Test finding with various suffixes
TEST_F(FindLineageTreeForName, FindLineageTree_WithFileEnding) {
   std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;

   lineage_trees["test.yaml"] = test_lineage_tree1;

   auto result1 = Initializer::findLineageTreeForName(lineage_trees, "test");
   ASSERT_TRUE(result1.has_value());
   EXPECT_EQ(result1.value().file, test_lineage_tree1.file);
}

// Test not found scenario
TEST_F(FindLineageTreeForName, FindLineageTree_NotFound) {
   std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;

   lineage_trees["completely_different_name"] = test_lineage_tree1;
   lineage_trees["another_name.yaml"] = test_lineage_tree2;

   auto result = Initializer::findLineageTreeForName(lineage_trees, "test");
   EXPECT_FALSE(result.has_value());
}

// Test empty map
TEST_F(FindLineageTreeForName, FindLineageTree_EmptyMap) {
   const std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;

   auto result = Initializer::findLineageTreeForName(lineage_trees, "test");
   EXPECT_FALSE(result.has_value());
}

// Test case sensitivity
TEST_F(FindLineageTreeForName, FindLineageTree_CaseSensitive) {
   std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;

   lineage_trees["Test"] = test_lineage_tree1;
   lineage_trees["TEST"] = test_lineage_tree2;

   auto result = Initializer::findLineageTreeForName(lineage_trees, "test");
   EXPECT_FALSE(result.has_value());  // Should not find due to case mismatch

   auto result_correct_case = Initializer::findLineageTreeForName(lineage_trees, "Test");
   ASSERT_TRUE(result_correct_case.has_value());
   EXPECT_EQ(result_correct_case.value().file, test_lineage_tree1.file);
}

// Test with special characters in name
TEST_F(FindLineageTreeForName, FindLineageTree_SpecialCharacters) {
   std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;

   lineage_trees["test-name_123"] = test_lineage_tree1;
   lineage_trees["lineage_definition_test-name_123.yaml"] = test_lineage_tree2;

   auto result = Initializer::findLineageTreeForName(lineage_trees, "test-name_123");
   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(result.value().file, test_lineage_tree1.file);
}
