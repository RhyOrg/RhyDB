#include "rhydb/database.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "config/source/yaml_file.h"
#include "rhydb/append/append_exception.h"
#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/preprocessing_config.h"
#include "rhydb/database_info.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/schema/builtin_tables.h"
#include "rhydb/storage/reference_genomes.h"
#include "rhydb/test/query_fixture.test.h"

using rhydb::config::PreprocessingConfig;

namespace {
std::shared_ptr<rhydb::Database> buildTestDatabase() {
   const std::filesystem::path input_directory{"./testBaseData/unitTestDummyDataset/"};

   auto config = PreprocessingConfig::withDefaults();
   config.overwriteFrom(
      rhydb::config::YamlFile::readFile(input_directory / "preprocessing_config.yaml")
         .verify(PreprocessingConfig::getConfigSpecification())
   );
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfigFromFile(
      input_directory / "database_config.yaml"
   );

   const auto reference_genomes =
      rhydb::ReferenceGenomes::readFromFile(config.initialization_files.getReferenceGenomeFilepath()
      );

   std::map<std::filesystem::path, rhydb::common::LineageTreeAndIdMap> lineage_trees;
   for (const auto& file_path : config.initialization_files.getLineageDefinitionFilepaths()) {
      lineage_trees[file_path] =
         rhydb::common::LineageTreeAndIdMap::fromLineageDefinitionFilePath(file_path);
   }

   rhydb::common::PhyloTree phylo_tree_file;
   auto opt_path = config.initialization_files.getPhyloTreeFilepath();
   if (opt_path.has_value()) {
      phylo_tree_file = rhydb::common::PhyloTree::fromFile(opt_path.value());
   }

   auto database = std::make_shared<rhydb::Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         database_config,
         reference_genomes,
         lineage_trees,
         phylo_tree_file,
         /*without_unaligned_sequences=*/false
      )
   );
   std::ifstream input(input_directory / "input.ndjson");
   database->appendData(rhydb::schema::TableName::getDefault(), input);
   return database;
}
}  // namespace

TEST(DatabaseTest, shouldSaveAndReloadDatabaseWithoutErrors) {
   auto first_database = buildTestDatabase();

   const std::filesystem::path directory = "testBaseData/rhydbSerializedState";

   const rhydb::DataVersion::Timestamp data_version_timestamp =
      first_database->getDataVersionTimestamp();

   first_database->saveDatabaseState(directory);

   const rhydb::RhyDBDataSource data_source =
      rhydb::RhyDBDataSource::checkValidDataSource(directory / data_version_timestamp.value);

   auto database = rhydb::Database::loadDatabaseState(data_source);

   const auto database_info = database.getDatabaseInfo();

   EXPECT_EQ(database_info.sequence_count, 5);
   EXPECT_GT(database_info.vertical_bitmaps_size, 0);
   EXPECT_GT(database_info.horizontal_bitmaps_size, 0);

   // When bumping the serialization version, run `make bump-serialization-version` which sets
   // RHYDB_KEEP_SERIALIZED_STATE=1 to preserve the produced directory for committing to Git.
   if (std::getenv("RHYDB_KEEP_SERIALIZED_STATE") == nullptr) {
      std::filesystem::remove_all(data_source.path);
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DatabaseTest, shouldReturnCorrectDatabaseInfoAfterAppendingNewSequences) {
   // If this load fails, the serialization version likely needs to be increased
   // Run `make bump-serialization-version`
   auto database = rhydb::Database::loadDatabaseState(
      rhydb::RhyDBDirectory{"testBaseData/rhydbSerializedState"}.getMostRecentDataDirectory().value(
      )
   );

   const auto database_info = database.getDatabaseInfo();
   auto data_version = database.getDataVersionTimestamp();

   EXPECT_EQ(database_info.sequence_count, 5);
   EXPECT_GT(database_info.vertical_bitmaps_size, 0);
   EXPECT_EQ(database_info.horizontal_bitmaps_size, 9);

   const std::string more_data =
      R"(
{"primaryKey": "key6", "pango_lineage": "XBB", "date": "2021-03-19", "region": "Europe", "country": "Switzerland", "division": "Solothurn", "unsorted_date": "2021-02-10", "age": 54, "qc_value": 0.94, "test_boolean_column": true, "float_value": null, "main": {"sequence": "ACGTACGT", "insertions": []}, "testSecondSequence": {"sequence": "ACGT", "insertions": []}, "unaligned_main": "ACGTACGT", "unaligned_testSecondSequence": "ACGT", "E": {"sequence": "MYSF*", "insertions": ["4:EPE"]}, "M": {"sequence": "XXXX*", "insertions": []}}
{"primaryKey": "key7", "pango_lineage": "B", "date": "2021-03-21", "region": "Europe", "country": "Switzerland", "division": "Basel", "unsorted_date": null, "age": null, "qc_value": 0.94, "test_boolean_column": true, "float_value": null, "main": {"sequence": "AAAAAAAA", "insertions": []}, "testSecondSequence": {"sequence": "ACAT", "insertions": []}, "unaligned_main": "AAAAAAAA", "unaligned_testSecondSequence": "ACAT", "E": {"sequence": "MYSF*", "insertions": ["4:EPE"]}, "M": {"sequence": "XXXX*", "insertions": []}}
)";
   std::stringstream more_data_stream{more_data};

   database.appendData(rhydb::schema::TableName::getDefault(), more_data_stream);

   const auto database_info_after_append = database.getDatabaseInfo();
   auto data_version_after_append = database.getDataVersionTimestamp();

   EXPECT_EQ(database_info_after_append.sequence_count, 7);
   EXPECT_GT(data_version_after_append, data_version);
}

TEST(DatabaseTest, newDatabaseContainsEmptyBuiltinTables) {
   const rhydb::Database database;

   for (const auto& [table_name, table_schema] : rhydb::schema::getBuiltinTableSchemas()) {
      ASSERT_TRUE(database.tables.contains(table_name)) << table_name.getName();
      ASSERT_TRUE(database.schema.tables.contains(table_name)) << table_name.getName();
      EXPECT_EQ(database.tables.at(table_name)->sequence_count, 0);
      EXPECT_EQ(
         database.schema.tables.at(table_name)->getColumnIdentifiers(),
         table_schema->getColumnIdentifiers()
      );
   }
}

TEST(DatabaseTest, loadedDatabaseContainsBuiltinTables) {
   // The committed serialized state may predate some built-in tables, which are then added on load
   const auto database = rhydb::Database::loadDatabaseState(
      rhydb::RhyDBDirectory{"testBaseData/rhydbSerializedState"}.getMostRecentDataDirectory().value(
      )
   );

   for (const auto& [table_name, _] : rhydb::schema::getBuiltinTableSchemas()) {
      EXPECT_TRUE(database.tables.contains(table_name)) << table_name.getName();
      EXPECT_TRUE(database.schema.tables.contains(table_name)) << table_name.getName();
   }
}

namespace {
// Counts the rows of the default table matching `filter` by running a SaneQL count aggregation.
int64_t countWhere(rhydb::Database& database, const std::string& filter) {
   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      fmt::format("default.filter({}).groupBy({{count:=count()}})", filter),
      database.tables,
      rhydb::config::QueryOptions{},
      "count_query"
   );
   auto result = rhydb::test::executeQueryToJsonArray(query_plan);
   if (result.empty()) {
      return 0;
   }
   return result.at(0).at("count").get<int64_t>();
}
}  // namespace

TEST(DatabaseTest, updateColumnAssignsScalarValueToMatchingRows) {
   auto database = buildTestDatabase();
   const std::string table = rhydb::schema::TableName::getDefault().getName();

   // Two rows (key1, key4) start with age 4; key3 has a null age.
   ASSERT_EQ(countWhere(*database, "age = 4"), 2);

   // Assign a scalar int to only the matching rows.
   database->updateColumn(table, "age", "100", "age = 4");
   ASSERT_EQ(countWhere(*database, "age = 4"), 0);
   ASSERT_EQ(countWhere(*database, "age = 100"), 2);

   // A previously-null value can be set to a concrete value.
   ASSERT_EQ(countWhere(*database, "age = 7"), 0);
   database->updateColumn(table, "age", "7", "primaryKey = 'key3'");
   ASSERT_EQ(countWhere(*database, "age = 7"), 1);

   // A SaneQL `null` literal clears the matched rows back to null.
   database->updateColumn(table, "age", "null", "primaryKey = 'key3'");
   ASSERT_EQ(countWhere(*database, "age = 7"), 0);
   ASSERT_EQ(countWhere(*database, "age.isNull()"), 1);

   // Bool values are parsed as the boolean literals 'true'/'false'.
   database->updateColumn(table, "test_boolean_column", "false", "true");
   ASSERT_EQ(countWhere(*database, "test_boolean_column = false"), 5);

   // Date values are SaneQL date literals.
   database->updateColumn(table, "date", "'2000-01-01'::date", "true");
   ASSERT_EQ(countWhere(*database, "date = '2000-01-01'::date"), 5);

   // Indexed string columns can be reassigned; the inverted index stays consistent. `value` is a
   // SaneQL string literal, so it is quoted.
   ASSERT_EQ(countWhere(*database, "division = 'Bern'"), 2);
   database->updateColumn(table, "division", "'Zurich'", "division = 'Bern'");
   ASSERT_EQ(countWhere(*database, "division = 'Bern'"), 0);
   ASSERT_EQ(countWhere(*database, "division = 'Zurich'"), 2);

   // A value not previously present in the dictionary is interned on update.
   database->updateColumn(table, "division", "'Geneva'", "primaryKey = 'key1'");
   ASSERT_EQ(countWhere(*database, "division = 'Geneva'"), 1);

   // A SaneQL `null` literal clears an indexed string back to null, and a concrete value can be set
   // again afterwards.
   database->updateColumn(table, "division", "null", "primaryKey = 'key1'");
   ASSERT_EQ(countWhere(*database, "division.isNull()"), 1);
   database->updateColumn(table, "division", "'Basel'", "primaryKey = 'key1'");
   ASSERT_EQ(countWhere(*database, "division.isNull()"), 0);
   ASSERT_EQ(countWhere(*database, "division = 'Basel'"), 1);
}

TEST(DatabaseTest, updateColumnRejectsInvalidRequests) {
   auto database = buildTestDatabase();
   const std::string table = rhydb::schema::TableName::getDefault().getName();

   // A literal that does not match the column's type is a query error.
   EXPECT_THAT(
      [&]() { database->updateColumn(table, "age", "'not_a_number'", "true"); },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("expected integer literal")
      )
   );

   // A string literal must be quoted; an int literal is not a valid string value.
   EXPECT_THAT(
      [&]() { database->updateColumn(table, "division", "5", "true"); },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("expected string literal")
      )
   );

   // A phylogenetic-tree-backed column (primaryKey) cannot be updated.
   EXPECT_THAT(
      [&]() { database->updateColumn(table, "primaryKey", "'new_key'", "true"); },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("phylogenetic tree")
      )
   );

   // A lineage-indexed column (pango_lineage) cannot be updated.
   EXPECT_THAT(
      [&]() { database->updateColumn(table, "pango_lineage", "'B.1'", "true"); },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(::testing::HasSubstr("lineage index"
      ))
   );

   // Unknown columns and tables are reported.
   EXPECT_THAT(
      [&]() { database->updateColumn(table, "does_not_exist", "1", "true"); },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("does not contain a column")
      )
   );
}

using rhydb::Nucleotide;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
using rhydb::schema::TableSchema;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::SequenceColumnMetadata;
using rhydb::storage::column::StringColumnMetadata;

TEST(DatabaseTest, canCreateMultipleTablesAndAddData) {
   rhydb::Database database;
   ColumnIdentifier primary_key{.name = "key", .type = ColumnType::STRING};
   ColumnIdentifier sequence_column{.name = "sequence", .type = ColumnType::NUCLEOTIDE_SEQUENCE};
   std::vector<Nucleotide::Symbol> reference_sequence{
      Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T
   };

   const std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
      {sequence_column,
       std::make_shared<SequenceColumnMetadata<Nucleotide>>(
          sequence_column.name, std::move(reference_sequence)
       )},
   };
   const rhydb::schema::TableName first_table_name{"first"};
   database.createTable(
      first_table_name, std::make_shared<TableSchema>(column_metadata, primary_key)
   );
   const rhydb::schema::TableName second_table_name{"second"};
   database.createTable(
      second_table_name, std::make_shared<TableSchema>(column_metadata, primary_key)
   );

   std::ifstream first_table_data{"testBaseData/example.ndjson"};
   database.appendData(first_table_name, first_table_data);

   auto query_plan_1 = rhydb::query_engine::Planner::planSaneqlQuery(
      "first.groupBy({count:=count()})",
      database.tables,
      rhydb::config::QueryOptions{},
      "test_query_1"
   );
   ASSERT_EQ(
      rhydb::test::executeQueryToJsonArray(query_plan_1), nlohmann::json::array({{{"count", 20}}})
   );

   std::stringstream second_table_data;
   second_table_data
      << R"({"key":"id_1","sequence":{"sequence":"AAAA","insertions":[],"offset":0}})";
   database.appendData(second_table_name, second_table_data);

   auto query_plan_2 = rhydb::query_engine::Planner::planSaneqlQuery(
      "second.groupBy({count:=count()})",
      database.tables,
      rhydb::config::QueryOptions{},
      "test_query_2"
   );
   ASSERT_EQ(
      rhydb::test::executeQueryToJsonArray(query_plan_2), nlohmann::json::array({{{"count", 1}}})
   );
}

namespace {
using rhydb::schema::TableName;

// The query options the API serves write statements with; the admin endpoint threads the runtime
// config's options through, so the tests use the same defaults.
rhydb::config::QueryOptions defaultQueryOptions() {
   return rhydb::config::RuntimeConfig::withDefaults().query_options;
}

// Builds a metadata-only table schema (string primary key + a string and an int32 column). Append
// queries copy query output back through the NDJSON append path, which round-trips value columns
// cleanly; sequence columns are intentionally left out (a query emits them as decompressed strings,
// while the append path ingests them as structured objects).
std::shared_ptr<TableSchema> makeValueColumnSchema() {
   const ColumnIdentifier key{.name = "key", .type = ColumnType::STRING};
   const ColumnIdentifier country{.name = "country", .type = ColumnType::STRING};
   const ColumnIdentifier age{.name = "age", .type = ColumnType::INT32};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {key, std::make_shared<StringColumnMetadata>(key.name)},
      {country, std::make_shared<StringColumnMetadata>(country.name)},
      {age, std::make_shared<ColumnMetadata>(age.name)},
   };
   return std::make_shared<TableSchema>(std::move(column_metadata), key);
}

// Runs a count aggregation over `table_name` filtered by `filter` and returns the matched row
// count.
int64_t countInTableWhere(
   rhydb::Database& database,
   const std::string& table_name,
   const std::string& filter
) {
   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      fmt::format("{}.filter({}).groupBy({{count:=count()}})", table_name, filter),
      database.tables,
      rhydb::config::QueryOptions{},
      "count_query"
   );
   auto result = rhydb::test::executeQueryToJsonArray(query_plan);
   if (result.empty()) {
      return 0;
   }
   return result.at(0).at("count").get<int64_t>();
}
}  // namespace

TEST(DatabaseInsertQueryTest, copiesFilteredRowsFromOneTableIntoAnother) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   database.createTable(TableName{"archive"}, makeValueColumnSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n"
               << R"({"key":"b","country":"US","age":2})" << "\n"
               << R"({"key":"c","country":"CH","age":3})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   const nlohmann::json result = database.executeWrite(
      "source.filter(country='CH').insertInto(archive)", defaultQueryOptions(), "test_request_id"
   );

   EXPECT_EQ(result.at("insertedRows").get<size_t>(), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "country='CH'"), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "country='US'"), 0);
   EXPECT_EQ(countInTableWhere(database, "archive", "age=1"), 1);
   EXPECT_EQ(countInTableWhere(database, "archive", "age=3"), 1);
   // The source table is left unchanged.
   EXPECT_EQ(countInTableWhere(database, "source", "true"), 3);
}

TEST(DatabaseInsertQueryTest, reshapesWithProjectAcceptsStringTargetAndAccumulates) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   // Target keeps only a subset of columns; the query must project down to match it.
   const ColumnIdentifier key{.name = "key", .type = ColumnType::STRING};
   const ColumnIdentifier country{.name = "country", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> archive_metadata{
      {key, std::make_shared<StringColumnMetadata>(key.name)},
      {country, std::make_shared<StringColumnMetadata>(country.name)},
   };
   database.createTable(
      TableName{"archive"}, std::make_shared<TableSchema>(std::move(archive_metadata), key)
   );

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n"
               << R"({"key":"b","country":"US","age":2})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   // Target named as a string literal; project drops the `age` column the target does not have.
   const nlohmann::json result = database.executeWrite(
      "source.project({key, country}).insertInto('archive')",
      defaultQueryOptions(),
      "test_request_id"
   );
   EXPECT_EQ(result.at("insertedRows").get<size_t>(), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 2);

   // A second append accumulates on top of the existing rows rather than replacing them.
   const nlohmann::json result_again = database.executeWrite(
      "source.filter(country='US').project({key, country}).insertInto(archive)",
      defaultQueryOptions(),
      "test_request_id"
   );
   EXPECT_EQ(result_again.at("insertedRows").get<size_t>(), 1);
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 3);
}

// The insert streams the query result into the target table batch by batch. With a small
// materialization cutoff the source query produces several batches, so this covers that the field
// order sniffed from the first line is reused for the later batches and that every batch lands.
TEST(DatabaseInsertQueryTest, insertsAResultThatSpansSeveralBatches) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   database.createTable(TableName{"archive"}, makeValueColumnSchema());

   constexpr size_t NUMBER_OF_ROWS = 25;
   std::stringstream source_data;
   for (size_t row = 0; row < NUMBER_OF_ROWS; ++row) {
      source_data << fmt::format(
                        R"({{"key":"key{}","country":"CH","age":{}}})", row, static_cast<int>(row)
                     )
                  << "\n";
   }
   database.appendData(TableName{"source"}, source_data);

   // A cutoff of 1 makes the table scan emit tiny batches, so the insert sees many of them.
   const nlohmann::json result = database.executeWrite(
      "source.insertInto(archive)",
      rhydb::config::QueryOptions{.materialization_cutoff = 1},
      "test_request_id"
   );

   EXPECT_EQ(result.at("insertedRows").get<size_t>(), NUMBER_OF_ROWS);
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), NUMBER_OF_ROWS);
   EXPECT_EQ(countInTableWhere(database, "archive", "country='CH'"), NUMBER_OF_ROWS);
   EXPECT_EQ(countInTableWhere(database, "archive", "age=24"), 1);
}

// The rows are inserted while the query is still producing, so a query that reads the table it
// writes into would read columns that the insert mutates underneath it.
TEST(DatabaseInsertQueryTest, rejectsInsertThatReadsItsTable) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   EXPECT_THAT(
      [&]() {
         database.executeWrite(
            "source.insertInto(source)", defaultQueryOptions(), "test_request_id"
         );
      },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("cannot write into table 'source' while the query reads from it")
      )
   );
   // The rejected write left the table untouched.
   EXPECT_EQ(countInTableWhere(database, "source", "true"), 1);
}

TEST(DatabaseInsertQueryTest, rejectsInvalidInsertQueries) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   database.createTable(TableName{"archive"}, makeValueColumnSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   // A plain read query is not a write statement.
   EXPECT_THAT(
      [&]() {
         database.executeWrite(
            "source.filter(country='CH')", defaultQueryOptions(), "test_request_id"
         );
      },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("expected a write statement")
      )
   );

   // The target table must exist.
   EXPECT_THAT(
      [&]() {
         database.executeWrite(
            "source.insertInto(does_not_exist)", defaultQueryOptions(), "test_request_id"
         );
      },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("target table 'does_not_exist' not found")
      )
   );

   // insertInto needs both a source and a target.
   EXPECT_THAT(
      [&]() {
         database.executeWrite("insertInto(source)", defaultQueryOptions(), "test_request_id");
      },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(
         ::testing::HasSubstr("insertInto() requires argument 'target'")
      )
   );
}

// A query that does not produce every column of the target is rejected outright: the target keeps
// exactly the rows it had, no prefix of the result is inserted.
TEST(DatabaseInsertQueryTest, insertsNothingWhenAColumnOfTheTargetIsMissing) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   database.createTable(TableName{"archive"}, makeValueColumnSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n"
               << R"({"key":"b","country":"US","age":2})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   std::stringstream archive_data;
   archive_data << R"({"key":"existing","country":"DE","age":9})" << "\n";
   database.appendData(TableName{"archive"}, archive_data);

   EXPECT_THAT(
      [&]() {
         database.executeWrite(
            "source.project({key}).insertInto(archive)", defaultQueryOptions(), "test_request_id"
         );
      },
      ThrowsMessage<rhydb::append::AppendException>(
         ::testing::HasSubstr("the column 'age' is not contained in the object")
      )
   );

   // The pre-existing row is untouched and nothing of the rejected query landed.
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 1);
   EXPECT_EQ(countInTableWhere(database, "archive", "key='existing'"), 1);
}

// The same contract for a result column whose type does not match the target column's type.
TEST(DatabaseInsertQueryTest, insertsNothingWhenAColumnHasTheWrongType) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   database.createTable(TableName{"archive"}, makeValueColumnSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   // `age` is an INT32 column in the target, the query hands it a string.
   EXPECT_THAT(
      [&]() {
         database.executeWrite(
            "source.project({key, country}).map({age := country}).insertInto(archive)",
            defaultQueryOptions(),
            "test_request_id"
         );
      },
      ThrowsMessage<rhydb::append::AppendException>(::testing::AllOf(
         ::testing::HasSubstr("error inserting into column 'age'"),
         ::testing::HasSubstr("error getting value as int32")
      ))
   );

   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 0);
}
