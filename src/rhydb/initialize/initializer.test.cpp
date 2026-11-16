#include "rhydb/initialize/initializer.h"

#include <algorithm>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "rhydb/database.h"
#include "rhydb/initialize/initialize_exception.h"
#include "rhydb/schema/builtin_tables.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

using rhydb::AminoAcid;
using rhydb::Nucleotide;
using rhydb::ReferenceGenomes;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::common::PhyloTree;
using rhydb::config::DatabaseConfig;
using rhydb::initialize::Initializer;
using rhydb::schema::DataType;
using rhydb::storage::column::DictionaryEncodedColumn;
using rhydb::storage::column::SequenceColumn;
using rhydb::storage::column::StringColumn;
using rhydb::storage::column::ZstdCompressedStringColumn;

TEST(Initializer, correctlyCreatesSchemaFromInitializationFiles) {
   const DatabaseConfig database_config = DatabaseConfig::getValidatedConfigFromFile(
      "testBaseData/unitTestDummyDataset/database_config.yaml"
   );
   const ReferenceGenomes reference_genomes =
      ReferenceGenomes::readFromFile("testBaseData/unitTestDummyDataset/reference_genomes.json");
   const PhyloTree phylo_tree_file =
      PhyloTree::fromNewickFile("testBaseData/unitTestDummyDataset/phylogenetic_tree.nwk");
   auto table_schema = Initializer::createSchemaFromConfigFiles(
      database_config,
      reference_genomes,
      phylo_tree_file,
      /*without_unaligned_sequences=*/false
   );

   const size_t expected_number_of_columns =
      database_config.schema.metadata.size() + reference_genomes.aa_sequence_names.size() +
      (reference_genomes.nucleotide_sequence_names.size() * 2);
   ASSERT_EQ(table_schema->getColumnIdentifiers().size(), expected_number_of_columns);
   ASSERT_TRUE(table_schema->primary_key.has_value());
   ASSERT_EQ(table_schema->primary_key->name, database_config.schema.primary_key.value());

   ASSERT_TRUE(table_schema->getColumn("M").has_value());
   ASSERT_EQ(table_schema->getColumn("M").value().type, DataType::AMINO_ACID_SEQUENCE);
   ASSERT_TRUE(table_schema->getColumnMetadata<SequenceColumn<AminoAcid>>("M").has_value());
   ASSERT_EQ(
      table_schema->getColumnMetadata<SequenceColumn<AminoAcid>>("M").value()->reference_sequence,
      reference_genomes.stringToVector<AminoAcid>("MADS*")
   );

   ASSERT_TRUE(table_schema->getColumn("age").has_value());
   ASSERT_EQ(table_schema->getColumn("age").value().type, DataType::INT32);

   ASSERT_TRUE(table_schema->getColumn("country").has_value());
   ASSERT_EQ(
      table_schema->getColumn("country").value().type, DataType::dictionaryEncoded(DataType::STRING)
   );
   ASSERT_TRUE(
      table_schema->getColumnMetadata<DictionaryEncodedColumn<StringColumn>>("country").has_value()
   );

   ASSERT_TRUE(table_schema->getColumn("date").has_value());
   ASSERT_EQ(table_schema->getColumn("date").value().type, DataType::DATE32);

   ASSERT_TRUE(table_schema->getColumn("division").has_value());
   ASSERT_EQ(
      table_schema->getColumn("division").value().type,
      DataType::dictionaryEncoded(DataType::STRING)
   );
   ASSERT_TRUE(
      table_schema->getColumnMetadata<DictionaryEncodedColumn<StringColumn>>("division").has_value()
   );

   ASSERT_TRUE(table_schema->getColumn("main").has_value());
   ASSERT_EQ(table_schema->getColumn("main").value().type, DataType::NUCLEOTIDE_SEQUENCE);
   ASSERT_TRUE(table_schema->getColumnMetadata<SequenceColumn<Nucleotide>>("main").has_value());
   ASSERT_EQ(
      table_schema->getColumnMetadata<SequenceColumn<Nucleotide>>("main")
         .value()
         ->reference_sequence,
      reference_genomes.stringToVector<Nucleotide>("ACGTACGT")
   );

   ASSERT_TRUE(table_schema->getColumn("pango_lineage").has_value());
   ASSERT_EQ(
      table_schema->getColumn("pango_lineage").value().type,
      DataType::dictionaryEncoded(DataType::STRING)
   );
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<DictionaryEncodedColumn<StringColumn>>("pango_lineage")
                  .has_value());

   ASSERT_TRUE(table_schema->getColumn("primaryKey").has_value());
   ASSERT_EQ(table_schema->getColumn("primaryKey").value().type, DataType::STRING);
   ASSERT_TRUE(table_schema->getColumnMetadata<StringColumn>("primaryKey").has_value());

   ASSERT_TRUE(table_schema->getColumn("qc_value").has_value());
   ASSERT_EQ(table_schema->getColumn("qc_value").value().type, DataType::FLOAT);

   ASSERT_TRUE(table_schema->getColumn("region").has_value());
   ASSERT_EQ(
      table_schema->getColumn("region").value().type, DataType::dictionaryEncoded(DataType::STRING)
   );
   ASSERT_TRUE(
      table_schema->getColumnMetadata<DictionaryEncodedColumn<StringColumn>>("region").has_value()
   );

   ASSERT_TRUE(table_schema->getColumn("testSecondSequence").has_value());
   ASSERT_EQ(
      table_schema->getColumn("testSecondSequence").value().type, DataType::NUCLEOTIDE_SEQUENCE
   );
   ASSERT_TRUE(
      table_schema->getColumnMetadata<SequenceColumn<Nucleotide>>("testSecondSequence").has_value()
   );
   ASSERT_EQ(
      table_schema->getColumnMetadata<SequenceColumn<Nucleotide>>("testSecondSequence")
         .value()
         ->reference_sequence,
      reference_genomes.stringToVector<Nucleotide>("ACGT")
   );

   ASSERT_TRUE(table_schema->getColumn("test_boolean_column").has_value());
   ASSERT_EQ(table_schema->getColumn("test_boolean_column").value().type, DataType::BOOL);

   ASSERT_TRUE(table_schema->getColumn("unaligned_main").has_value());
   ASSERT_EQ(
      table_schema->getColumn("unaligned_main").value().type, DataType::ZSTD_COMPRESSED_STRING
   );
   ASSERT_TRUE(
      table_schema->getColumnMetadata<ZstdCompressedStringColumn>("unaligned_main").has_value()
   );
   ASSERT_EQ(
      table_schema->getColumnMetadata<ZstdCompressedStringColumn>("unaligned_main")
         .value()
         ->dictionary_string,
      "ACGTACGT"
   );

   ASSERT_TRUE(table_schema->getColumn("unaligned_testSecondSequence").has_value());
   ASSERT_EQ(
      table_schema->getColumn("unaligned_testSecondSequence").value().type,
      DataType::ZSTD_COMPRESSED_STRING
   );
   ASSERT_TRUE(table_schema
                  ->getColumnMetadata<ZstdCompressedStringColumn>("unaligned_testSecondSequence")
                  .has_value());
   ASSERT_EQ(
      table_schema->getColumnMetadata<ZstdCompressedStringColumn>("unaligned_testSecondSequence")
         .value()
         ->dictionary_string,
      "ACGT"
   );

   ASSERT_TRUE(table_schema->getColumn("unsorted_date").has_value());
   ASSERT_EQ(table_schema->getColumn("unsorted_date").value().type, DataType::DATE32);

   ASSERT_TRUE(table_schema->primary_key.has_value());
   ASSERT_EQ(table_schema->primary_key->name, "primaryKey");
   ASSERT_EQ(table_schema->primary_key->type, DataType::STRING);
}

TEST(Initializer, rejectsCreatingTableWithNameOfExistingTable) {
   const auto database_config = DatabaseConfig::getValidatedConfigFromFile(
      "testBaseData/unitTestDummyDataset/database_config.yaml"
   );
   const ReferenceGenomes reference_genomes =
      ReferenceGenomes::readFromFile("testBaseData/unitTestDummyDataset/reference_genomes.json");
   rhydb::Database database;
   EXPECT_THROW(
      Initializer::createTableInDatabase(
         rhydb::schema::TableName{std::string{rhydb::schema::REFERENCE_GENOMES_TABLE_NAME}},
         database_config,
         reference_genomes,
         {},
         PhyloTree{},
         /*without_unaligned_sequences=*/false,
         database
      ),
      rhydb::initialize::InitializeException
   );
}

TEST(Initializer, prefixesPreprocessedNamesOfTablesOtherThanTheDefaultTable) {
   EXPECT_EQ(
      Initializer::preprocessedName(rhydb::schema::TableName::getDefault(), "lineage"), "lineage"
   );
   EXPECT_EQ(
      Initializer::preprocessedName(rhydb::schema::TableName{"dengue"}, "lineage"), "dengue_lineage"
   );
}

namespace {

/// Creates `table_name` in `database` from the initialization files of the unit test dummy dataset.
void createDummyTable(const std::string& table_name, rhydb::Database& database) {
   std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees;
   lineage_trees.emplace(
      "test_lineage_definition.yaml",
      LineageTreeAndIdMap::fromLineageDefinitionFile(
         rhydb::preprocessing::LineageDefinitionFile::fromYAMLString(
            "A: ~\nB:\n  parents:\n    - A\n"
         )
      )
   );
   Initializer::createTableInDatabase(
      rhydb::schema::TableName{table_name},
      DatabaseConfig::getValidatedConfigFromFile(
         "testBaseData/unitTestDummyDataset/database_config.yaml"
      ),
      ReferenceGenomes::readFromFile("testBaseData/unitTestDummyDataset/reference_genomes.json"),
      lineage_trees,
      PhyloTree(),
      /*without_unaligned_sequences=*/false,
      database
   );
}

/// The values of the string column `column_name` of the table `table_name`, in row order.
std::vector<std::string> stringValues(
   const rhydb::Database& database,
   std::string_view table_name,
   const std::string& column_name
) {
   const auto& table = *database.tables.at(rhydb::schema::TableName{std::string{table_name}});
   const auto& column = table.getColumn<StringColumn>(column_name);
   std::vector<std::string> values;
   for (const auto row_id : table.row_layout) {
      values.emplace_back(column.getValueString(row_id));
   }
   return values;
}

}  // namespace

TEST(Initializer, createsSeveralTablesWithPrefixedReferencesAndLineageTables) {
   rhydb::Database database;
   createDummyTable(rhydb::schema::TableName::getDefault().getName(), database);
   createDummyTable("second", database);

   const std::vector<std::string> lineage_tables{
      "pango_lineage",
      "pango_lineage_aliases",
      "second_pango_lineage",
      "second_pango_lineage_aliases",
   };
   for (const auto& lineage_table : lineage_tables) {
      EXPECT_TRUE(database.tables.contains(rhydb::schema::TableName{lineage_table}))
         << lineage_table;
   }
   EXPECT_EQ(
      stringValues(
         database,
         rhydb::schema::LINEAGE_DEFINITIONS_TABLE_NAME,
         std::string{rhydb::schema::LINEAGE_DEFINITIONS_COLUMN_NAME_COLUMN}
      ),
      (std::vector<std::string>{"pango_lineage", "second_pango_lineage"})
   );

   const auto reference_genomes =
      ReferenceGenomes::readFromFile("testBaseData/unitTestDummyDataset/reference_genomes.json");
   const auto names = stringValues(database, rhydb::schema::REFERENCE_GENOMES_TABLE_NAME, "name");
   const auto& sequence_name = reference_genomes.nucleotide_sequence_names.at(0);
   EXPECT_EQ(std::ranges::count(names, sequence_name), 1);
   EXPECT_EQ(std::ranges::count(names, "second_" + sequence_name), 1);
   EXPECT_EQ(
      names.size(),
      2 * (reference_genomes.nucleotide_sequence_names.size() +
           reference_genomes.aa_sequence_names.size())
   );
}

TEST(Initializer, rejectsTableWhoseLineageTablesExist) {
   rhydb::Database database;
   database.createTable(
      rhydb::schema::TableName{"second_pango_lineage"},
      std::make_shared<rhydb::schema::TableSchema>()
   );
   EXPECT_THROW(createDummyTable("second", database), rhydb::initialize::InitializeException);
}

namespace {

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

}  // namespace

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
