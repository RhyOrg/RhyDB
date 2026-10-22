#include "rhydb/database.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "config/source/yaml_file.h"
#include "rhydb/append/append_exception.h"
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

   const auto reference_genomes = rhydb::ReferenceGenomes::readFromFile(
      config.initialization_files.getReferenceGenomeFilepath()
   );

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

   EXPECT_EQ(database_info.row_count, 5);
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
      rhydb::RhyDBDirectory{"testBaseData/rhydbSerializedState"}
         .getMostRecentDataDirectory()
         .value()
   );

   const auto database_info = database.getDatabaseInfo();
   auto data_version = database.getDataVersionTimestamp();

   EXPECT_EQ(database_info.row_count, 5);
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

   EXPECT_EQ(database_info_after_append.row_count, 7);
   EXPECT_GT(data_version_after_append, data_version);
}

TEST(DatabaseTest, newDatabaseContainsEmptyBuiltinTables) {
   const rhydb::Database database;

   for (const auto& [table_name, table_schema] : rhydb::schema::getBuiltinTableSchemas()) {
      ASSERT_TRUE(database.tables.contains(table_name)) << table_name.getName();
      ASSERT_TRUE(database.schema.tables.contains(table_name)) << table_name.getName();
      EXPECT_EQ(database.tables.at(table_name)->row_count, 0);
      EXPECT_EQ(
         database.schema.tables.at(table_name)->getColumnIdentifiers(),
         table_schema->getColumnIdentifiers()
      );
   }
}

TEST(DatabaseTest, loadedDatabaseContainsBuiltinTables) {
   // The committed serialized state may predate some built-in tables, which are then added on load
   const auto database = rhydb::Database::loadDatabaseState(
      rhydb::RhyDBDirectory{"testBaseData/rhydbSerializedState"}
         .getMostRecentDataDirectory()
         .value()
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
      fmt::format("data.filter({}).group(by:={{}}, aggs:={{count:=count()}})", filter),
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

namespace {
// The rows of the default table as a map from primary key to the values of `columns`.
std::map<std::string, nlohmann::json> rowsByKey(
   rhydb::Database& database,
   const std::string& columns
) {
   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      fmt::format("data.project({{primaryKey, {}}})", columns),
      database.tables,
      rhydb::config::QueryOptions{},
      "rows_query"
   );
   std::map<std::string, nlohmann::json> rows;
   for (const auto& row : rhydb::test::executeQueryToJsonArray(query_plan)) {
      rows[row.at("primaryKey").get<std::string>()] = row;
   }
   return rows;
}
}  // namespace

namespace {
// Runs the SaneQL write statement `statement` and returns the number of rows it updated.
size_t update(rhydb::Database& database, const std::string& statement) {
   return rhydb::test::writeResultToJson(
             database.executeWrite(statement, rhydb::config::QueryOptions{}, "update_query")
   )
      .at("updatedRows")
      .get<size_t>();
}
}  // namespace

TEST(DatabaseUpdateQueryTest, assignsValuesToTheRowsMatchingWhere) {
   auto database = buildTestDatabase();

   // Two rows (key1, key4) start with age 4; key3 has a null age.
   ASSERT_EQ(countWhere(*database, "age = 4"), 2);

   // Assign a scalar int to only the matching rows.
   EXPECT_EQ(update(*database, "update(data, set := {age := 100}, where := age = 4)"), 2);
   ASSERT_EQ(countWhere(*database, "age = 4"), 0);
   ASSERT_EQ(countWhere(*database, "age = 100"), 2);

   // `set` can be passed positionally, and the table as the receiver of a method call.
   EXPECT_EQ(update(*database, "data.update({age := 7}, where := primaryKey = 'key3')"), 1);
   ASSERT_EQ(countWhere(*database, "age = 7"), 1);

   // A SaneQL `null` literal clears the matched rows back to null.
   update(*database, "update(data, set := {age := null}, where := primaryKey = 'key3')");
   ASSERT_EQ(countWhere(*database, "age = 7"), 0);
   ASSERT_EQ(countWhere(*database, "age.isNull()"), 1);

   // Without `where`, every row is updated.
   EXPECT_EQ(update(*database, "update(data, set := {test_boolean_column := false})"), 5);
   ASSERT_EQ(countWhere(*database, "test_boolean_column = false"), 5);

   // A predicate that matches no rows updates nothing.
   EXPECT_EQ(update(*database, "update(data, set := {age := 1}, where := age = 12345)"), 0);
   ASSERT_EQ(countWhere(*database, "age = 1"), 0);
}

TEST(DatabaseUpdateQueryTest, assignsSeveralColumnsFromTheValuesBeforeTheUpdate) {
   auto database = buildTestDatabase();
   ASSERT_EQ(countWhere(*database, "division = 'Bern' && age = 4"), 1);

   // The rows are selected before anything is assigned, so the predicate sees the old values even
   // though it reads columns that are assigned.
   EXPECT_EQ(
      update(
         *database,
         "update(data, set := {age := 5, division := 'Basel'}, "
         "where := division = 'Bern' && age = 4)"
      ),
      1
   );
   EXPECT_EQ(countWhere(*database, "division = 'Basel' && age = 5"), 1);
   EXPECT_EQ(countWhere(*database, "division = 'Bern'"), 1);
   EXPECT_EQ(countWhere(*database, "age = 4"), 1);
}

TEST(DatabaseUpdateQueryTest, selectsRowsByAPredicateOnASequenceColumn) {
   auto database = buildTestDatabase();
   ASSERT_EQ(countWhere(*database, "main.at(2) = 'A'"), 1);
   ASSERT_EQ(countWhere(*database, "age = 42"), 0);

   // The predicate sees the sequence columns as queries do, so it may read their symbols.
   EXPECT_EQ(update(*database, "update(data, set := {age := 42}, where := main.at(2) = 'A')"), 1);
   EXPECT_EQ(countWhere(*database, "age = 42"), 1);
   EXPECT_EQ(countWhere(*database, "main.at(2) = 'A' && age = 42"), 1);
}

TEST(DatabaseUpdateQueryTest, assignsDatesAndIndexedStrings) {
   auto database = buildTestDatabase();

   // Date values are SaneQL date literals.
   EXPECT_EQ(update(*database, "update(data, set := {date := '2000-01-01'::date})"), 5);
   ASSERT_EQ(countWhere(*database, "date = '2000-01-01'::date"), 5);

   // Indexed string columns can be reassigned; the inverted index stays consistent.
   ASSERT_EQ(countWhere(*database, "division = 'Bern'"), 2);
   update(*database, "update(data, set := {division := 'Zurich'}, where := division = 'Bern')");
   ASSERT_EQ(countWhere(*database, "division = 'Bern'"), 0);
   ASSERT_EQ(countWhere(*database, "division = 'Zurich'"), 2);

   // A value not previously present in the dictionary is interned on update.
   update(*database, "update(data, set := {division := 'Geneva'}, where := primaryKey = 'key1')");
   ASSERT_EQ(countWhere(*database, "division = 'Geneva'"), 1);

   // A SaneQL `null` literal clears an indexed string back to null, and a concrete value can be set
   // again afterwards.
   update(*database, "update(data, set := {division := null}, where := primaryKey = 'key1')");
   ASSERT_EQ(countWhere(*database, "division.isNull()"), 1);
   update(*database, "update(data, set := {division := 'Basel'}, where := primaryKey = 'key1')");
   ASSERT_EQ(countWhere(*database, "division.isNull()"), 0);
   ASSERT_EQ(countWhere(*database, "division = 'Basel'"), 1);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DatabaseUpdateQueryTest, computesValuesFromTheRowsColumns) {
   auto database = buildTestDatabase();
   const auto before = rowsByKey(*database, "division, country, age, qc_value");

   // A value can be computed by a scalar function of the row's columns.
   update(*database, "update(data, set := {region := at(country, 1)})");
   // Values are computed per row from the table before the update, so two columns can be swapped.
   update(*database, "update(data, set := {division := country, country := division})");
   // An integer can be assigned to a float column.
   update(*database, "update(data, set := {qc_value := age}, where := primaryKey <> 'key1')");

   const auto after = rowsByKey(*database, "region, division, country, qc_value");
   ASSERT_EQ(after.size(), before.size());
   for (const auto& [key, row] : after) {
      const auto& old_row = before.at(key);
      EXPECT_EQ(row.at("division"), old_row.at("country")) << key;
      EXPECT_EQ(row.at("country"), old_row.at("division")) << key;
      if (key == "key1") {
         EXPECT_EQ(row.at("qc_value"), old_row.at("qc_value")) << key;
      } else if (old_row.at("age").is_null()) {
         EXPECT_TRUE(row.at("qc_value").is_null()) << key;
      } else {
         EXPECT_EQ(row.at("qc_value").get<double>(), old_row.at("age").get<double>()) << key;
      }
      if (old_row.at("country").is_null()) {
         EXPECT_TRUE(row.at("region").is_null()) << key;
      } else {
         EXPECT_EQ(
            row.at("region").get<std::string>(),
            old_row.at("country").get<std::string>().substr(0, 1)
         ) << key;
      }
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DatabaseUpdateQueryTest, readsSequenceAndCompressedColumnsDecompressed) {
   auto database = buildTestDatabase();
   const auto before = rowsByKey(*database, "main, unaligned_main");

   // Sequence and compressed string columns are read decompressed, as in queries.
   update(*database, "update(data, set := {division := unaligned_main, country := at(main, 2)})");

   const auto after = rowsByKey(*database, "division, country");
   ASSERT_EQ(after.size(), before.size());
   for (const auto& [key, row] : after) {
      const auto& old_row = before.at(key);
      EXPECT_EQ(row.at("division"), old_row.at("unaligned_main")) << key;
      if (old_row.at("main").is_null()) {
         EXPECT_TRUE(row.at("country").is_null()) << key;
      } else {
         EXPECT_EQ(
            row.at("country").get<std::string>(), old_row.at("main").get<std::string>().substr(1, 1)
         ) << key;
      }
   }
}

TEST(DatabaseUpdateQueryTest, rejectsInvalidStatements) {
   auto database = buildTestDatabase();

   auto expect_rejected = [&](const std::string& statement, const std::string& message) {
      EXPECT_THAT(
         [&]() { update(*database, statement); },
         ThrowsMessage<rhydb::query_engine::IllegalQueryException>(::testing::HasSubstr(message))
      ) << statement;
   };

   expect_rejected(
      "update(data, set := {age := 'not_a_number'})",
      "has type 'STRING', which cannot be assigned to a column of type 'INT32'"
   );
   expect_rejected(
      "update(data, set := {division := 5})",
      "has type 'INT64', which cannot be assigned to a column of type 'DICTIONARY_ENCODED(STRING)'"
   );
   expect_rejected(
      "update(data, set := {age := qc_value})",
      "has type 'FLOAT', which cannot be assigned to a column of type 'INT32'"
   );
   // Sequence columns can be read, but not updated.
   expect_rejected(
      "update(data, set := {main := 'ACGTACGT'})", "Updating columns of type 'NUCLEOTIDE_SEQUENCE'"
   );
   expect_rejected(
      "update(data, set := {E := 'MYSF*'})", "Updating columns of type 'AMINO_ACID_SEQUENCE'"
   );
   expect_rejected("update(data, set := {primaryKey := 'new_key'})", "phylogenetic tree");
   expect_rejected("update(data, set := {does_not_exist := 1})", "does not contain a column");
   expect_rejected("update(does_not_exist, set := {age := 1})", "not found in database");
   expect_rejected("update(data, set := {age := 1, age := 2})", "more than once");
   expect_rejected("update(data, set := age)", "expects `set` to be a record");
   expect_rejected("update(data, set := {})", "expects `set` to be a record");
   expect_rejected("update(data, where := age = 4)", "update() requires argument 'set'");
   expect_rejected(
      "update(data, set := {age := 1}, where := unknown = 4)", "references unknown column 'unknown'"
   );
   // The predicate is only accepted by name, like SQL's WHERE clause.
   expect_rejected(
      "update(data, {age := 1}, age = 4)", "update() received too many positional arguments"
   );

   // An invalid assignment rejects the whole statement, so no other column is updated either.
   ASSERT_EQ(countWhere(*database, "age = 1"), 0);
   expect_rejected("update(data, set := {age := 1, division := 5})", "cannot be assigned");
   EXPECT_EQ(countWhere(*database, "age = 1"), 0);

   // A value that does not fit the column is only detected while computing it, but still before
   // anything is written.
   EXPECT_THAT(
      [&]() { update(*database, "update(data, set := {qc_value := 1, age := 3000000000})"); },
      ThrowsMessage<rhydb::query_engine::IllegalQueryException>(::testing::HasSubstr("3000000000"))
   );
   EXPECT_EQ(countWhere(*database, "qc_value = 1.0"), 0);
}

namespace {
// Runs the SaneQL write statement `statement` and returns the number of rows it deleted.
size_t deleteRows(rhydb::Database& database, const std::string& statement) {
   return rhydb::test::writeResultToJson(
             database.executeWrite(statement, rhydb::config::QueryOptions{}, "delete_query")
   )
      .at("deletedRows")
      .get<size_t>();
}

const std::string ALL_COLUMNS =
   "date, unsorted_date, region, country, pango_lineage, division, age, qc_value, "
   "test_boolean_column, main, testSecondSequence, unaligned_main, unaligned_testSecondSequence, "
   "E, M";

// A row of the test dataset, varied by `index` so that every column has several distinct values,
// nulls, mutations, missing symbols and insertions.
std::string generatedRow(const std::string& key, size_t index, const std::string& region) {
   const std::array<std::string, 4> main_sequences{"ACGTACGT", "AAGTACGT", "ACGTNNGT", "ACCTACGT"};
   const std::array<std::string, 3> countries{"Switzerland", "Germany", "France"};
   const std::array<std::string, 3> lineages{"B.1.1.7", "XBB", "B"};
   const std::array<std::string, 4> divisions{"\"Bern\"", "\"Basel\"", "\"Zurich\"", "null"};
   const std::array<std::string, 3> booleans{"true", "false", "null"};
   const std::string& main = main_sequences.at(index % 4);
   const std::string country =
      index % 7 == 0 ? "null" : fmt::format("\"{}\"", countries.at(index % 3));
   return fmt::format(
      R"({{"primaryKey": "{}", "date": "2021-01-{:02}", "unsorted_date": {}, "region": "{}", )"
      R"("country": {}, "pango_lineage": "{}", "division": {}, "age": {}, "qc_value": {}, )"
      R"("test_boolean_column": {}, "main": {}, "testSecondSequence": {{"sequence": "ACGT", )"
      R"("insertions": []}}, "unaligned_main": {}, "unaligned_testSecondSequence": "ACGT", )"
      R"("E": {{"sequence": "MYSF*", "insertions": [{}]}}, "M": {{"sequence": "{}", )"
      R"("insertions": []}}}})",
      key,
      1 + (index % 28),
      index % 5 == 0 ? "null" : fmt::format("\"2021-02-{:02}\"", 28 - (index % 28)),
      region,
      country,
      lineages.at(index % 3),
      divisions.at(index % 4),
      index % 6 == 0 ? "null" : std::to_string(index % 10),
      0.9 + static_cast<double>(index % 10) / 100.0,
      booleans.at(index % 3),
      index % 9 == 0 ? "null"
                     : fmt::format(
                          R"({{"sequence": "{}", "insertions": [{}]}})",
                          main,
                          index % 5 == 1 ? R"("3:TT")" : ""
                       ),
      index % 9 == 0 ? "null" : fmt::format("\"{}\"", main),
      index % 4 == 2 ? R"("4:EPE")" : "",
      index % 2 == 0 ? "MADS*" : "XXXX*"
   );
}

// The test dataset with three further batches appended, each of which becomes its own chunk. The
// rows of the second batch are the only ones in the region 'Africa'.
std::shared_ptr<rhydb::Database> buildMultiChunkTestDatabase() {
   auto database = buildTestDatabase();
   const std::array<std::pair<size_t, std::string>, 3> batches{
      std::pair<size_t, std::string>{7, "Asia"},
      std::pair<size_t, std::string>{5, "Africa"},
      std::pair<size_t, std::string>{9, "Asia"},
   };
   size_t index = 0;
   for (const auto& [size, region] : batches) {
      std::string batch;
      for (size_t row = 0; row < size; ++row, ++index) {
         batch += generatedRow(fmt::format("gen{}", index), index, region) + "\n";
      }
      std::stringstream batch_stream{batch};
      database->appendData(rhydb::schema::TableName::getDefault(), batch_stream);
   }
   return database;
}

const rhydb::storage::Table& defaultTable(const rhydb::Database& database) {
   return *database.tables.at(rhydb::schema::TableName::getDefault());
}
}  // namespace

TEST(DatabaseDeleteQueryTest, deletesTheRowsMatchingWhere) {
   auto database = buildTestDatabase();
   const auto before = rowsByKey(*database, ALL_COLUMNS);
   ASSERT_EQ(before.size(), 5);

   EXPECT_EQ(deleteRows(*database, "delete(data, where := primaryKey = 'key2')"), 1);
   auto expected = before;
   expected.erase("key2");
   EXPECT_EQ(rowsByKey(*database, ALL_COLUMNS), expected);
   EXPECT_EQ(countWhere(*database, "true"), 4);

   // The table can be the receiver of a method call.
   EXPECT_EQ(deleteRows(*database, "data.delete(where := age = 4)"), 2);
   expected.erase("key1");
   expected.erase("key4");
   EXPECT_EQ(rowsByKey(*database, ALL_COLUMNS), expected);

   // A predicate that matches no rows deletes nothing.
   EXPECT_EQ(deleteRows(*database, "delete(data, where := age = 12345)"), 0);
   EXPECT_EQ(rowsByKey(*database, ALL_COLUMNS), expected);

   // Without `where`, every row is deleted.
   EXPECT_EQ(deleteRows(*database, "delete(data)"), 2);
   EXPECT_EQ(countWhere(*database, "true"), 0);
   EXPECT_EQ(defaultTable(*database).row_layout.numChunks(), 0);
   EXPECT_EQ(database->getDatabaseInfo().row_count, 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DatabaseDeleteQueryTest, keepsEveryColumnAndIndexConsistentAcrossChunks) {
   const auto reference = buildMultiChunkTestDatabase();
   const auto reference_rows = rowsByKey(*reference, ALL_COLUMNS);
   ASSERT_EQ(defaultTable(*reference).row_layout.numChunks(), 4);

   // Filters that read every kind of column and index, which must select the same remaining rows
   // after the deletion as before it.
   const std::vector<std::string> probes{
      "true",
      "primaryKey = 'gen3'",
      "age = 4",
      "age < 5",
      "age.isNull()",
      "qc_value < 0.95",
      "qc_value.isNull()",
      "date < '2021-01-05'::date",
      "date.isNull()",
      "unsorted_date > '2021-02-20'::date",
      "unsorted_date.isNull()",
      "test_boolean_column = true",
      "test_boolean_column = false",
      "test_boolean_column.isNull()",
      "division = 'Bern'",
      "division.isNull()",
      "country = 'Germany'",
      "country.isNull()",
      "region = 'Asia'",
      "pango_lineage = 'XBB'",
      "pango_lineage = 'B'",
      "phyloDescendantOf(primaryKey, 'inner2')",
      "main.at(2) = 'A'",
      "main.at(3) = 'C'",
      "main.at(5) = 'N'",
      "main.isNull()",
      "M.at(1) = 'X'",
      "insertionContains(position := 3, value := 'TT', sequenceName := 'main')",
      "aminoAcidInsertionContains(position := 4, value := 'EPE', sequenceName := 'E')",
   };

   const std::vector<std::string> deletions{
      // a single row of the first chunk, which is bound to a node of the phylogenetic tree
      "primaryKey = 'key3'",
      // rows scattered over all chunks
      "age = 4 || division.isNull() || main.at(3) = 'C'",
      // all rows of the third chunk, so that the last chunk moves down
      "region = 'Africa'",
      // all rows of the first and of the last chunk
      "!(region = 'Africa')",
      // rows selected by their sequences
      "main.at(2) = 'A' || insertionContains(position := 3, value := 'TT', sequenceName := 'main')",
   };

   for (const auto& deletion : deletions) {
      SCOPED_TRACE(deletion);
      auto database = buildMultiChunkTestDatabase();
      const auto expected_deleted = static_cast<size_t>(countWhere(*reference, deletion));
      ASSERT_GT(expected_deleted, 0);

      EXPECT_EQ(
         deleteRows(*database, fmt::format("delete(data, where := {})", deletion)),
         expected_deleted
      );

      const auto& table = defaultTable(*database);
      EXPECT_EQ(table.row_layout.numRows(), reference_rows.size() - expected_deleted);
      EXPECT_NO_THROW(table.validate());

      auto expected_rows = reference_rows;
      for (const auto& [key, row] : rowsByKey(*reference, "primaryKey")) {
         if (countWhere(*reference, fmt::format("primaryKey = '{}' && ({})", key, deletion)) > 0) {
            expected_rows.erase(key);
         }
      }
      EXPECT_EQ(rowsByKey(*database, ALL_COLUMNS), expected_rows);

      for (const auto& probe : probes) {
         EXPECT_EQ(
            countWhere(*database, probe),
            countWhere(*reference, fmt::format("({}) && !({})", probe, deletion))
         ) << probe;
      }
   }
}

TEST(DatabaseDeleteQueryTest, dropsChunksWithoutRemainingRows) {
   auto database = buildMultiChunkTestDatabase();
   ASSERT_EQ(defaultTable(*database).row_layout.numChunks(), 4);

   EXPECT_EQ(deleteRows(*database, "delete(data, where := region = 'Africa')"), 5);
   EXPECT_EQ(defaultTable(*database).row_layout.numChunks(), 3);
   EXPECT_EQ(countWhere(*database, "region = 'Asia'"), 16);
}

TEST(DatabaseDeleteQueryTest, rowsCanBeAppendedAndSavedAfterADeletion) {
   auto database = buildMultiChunkTestDatabase();
   deleteRows(*database, "delete(data, where := age = 4 || region = 'Africa')");

   // The phylogenetic tree node of a deleted key is unbound, so the key can be inserted again.
   ASSERT_EQ(countWhere(*database, "phyloDescendantOf(primaryKey, 'inner2')"), 1);
   std::stringstream reinserted{generatedRow("key4", 3, "Europe") + "\n"};
   database->appendData(rhydb::schema::TableName::getDefault(), reinserted);
   EXPECT_EQ(countWhere(*database, "phyloDescendantOf(primaryKey, 'inner2')"), 2);
   EXPECT_EQ(countWhere(*database, "primaryKey = 'key4'"), 1);

   const auto rows = rowsByKey(*database, ALL_COLUMNS);

   const std::filesystem::path directory = "testBaseData/deleteQueryTestState";
   database->saveDatabaseState(directory);
   auto reloaded = rhydb::Database::loadDatabaseState(
      rhydb::RhyDBDirectory{directory}.getMostRecentDataDirectory().value()
   );
   std::filesystem::remove_all(directory);

   EXPECT_EQ(rowsByKey(reloaded, ALL_COLUMNS), rows);
   EXPECT_EQ(countWhere(reloaded, "phyloDescendantOf(primaryKey, 'inner2')"), 2);
}

TEST(DatabaseDeleteQueryTest, rejectsInvalidStatements) {
   auto database = buildTestDatabase();

   auto expect_rejected = [&](const std::string& statement, const std::string& message) {
      EXPECT_THAT(
         [&]() { deleteRows(*database, statement); },
         ThrowsMessage<rhydb::query_engine::IllegalQueryException>(::testing::HasSubstr(message))
      ) << statement;
   };

   expect_rejected("delete(does_not_exist)", "not found in database");
   expect_rejected("delete(data, where := unknown = 4)", "references unknown column 'unknown'");
   // The predicate is only accepted by name, like SQL's WHERE clause.
   expect_rejected("delete(data, age = 4)", "delete() received too many positional arguments");

   EXPECT_EQ(countWhere(*database, "true"), 5);
}

using rhydb::Nucleotide;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::DataType;
using rhydb::schema::TableSchema;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::SequenceColumnMetadata;
using rhydb::storage::column::StringColumnMetadata;

TEST(DatabaseTest, canCreateMultipleTablesAndAddData) {
   rhydb::Database database;
   ColumnIdentifier primary_key{.name = "key", .type = DataType::STRING};
   ColumnIdentifier sequence_column{.name = "sequence", .type = DataType::NUCLEOTIDE_SEQUENCE};
   std::vector<Nucleotide::Symbol> reference_sequence{
      Nucleotide::Symbol::A,
      Nucleotide::Symbol::C,
      Nucleotide::Symbol::G,
      Nucleotide::Symbol::T,
   };

   const std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
      {
         sequence_column,
         std::make_shared<SequenceColumnMetadata<Nucleotide>>(
            sequence_column.name, std::move(reference_sequence)
         ),
      },
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
      "first.group(by:={}, aggs:={count:=count()})",
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
      "second.group(by:={}, aggs:={count:=count()})",
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
   const ColumnIdentifier key{.name = "key", .type = DataType::STRING};
   const ColumnIdentifier country{.name = "country", .type = DataType::STRING};
   const ColumnIdentifier age{.name = "age", .type = DataType::INT32};
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
      fmt::format("{}.filter({}).group(by:={{}}, aggs:={{count:=count()}})", table_name, filter),
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

TEST(DatabaseTest, getFilteredBitmapSupportsSubqueries) {
   rhydb::Database database;
   database.createTable(TableName{"people"}, makeValueColumnSchema());
   database.createTable(TableName{"selected"}, makeValueColumnSchema());

   std::stringstream people_data;
   people_data << R"({"key":"a","country":"CH","age":1})" << "\n"
               << R"({"key":"b","country":"US","age":2})" << "\n"
               << R"({"key":"c","country":"DE","age":3})" << "\n";
   database.appendData(TableName{"people"}, people_data);
   std::stringstream selected_data;
   selected_data << R"({"key":"x","country":"DE","age":0})" << "\n"
                 << R"({"key":"y","country":"CH","age":0})" << "\n";
   database.appendData(TableName{"selected"}, selected_data);

   const roaring::Roaring bitmap =
      database.getFilteredBitmap("people", "country.in(selected.project({country}))");
   EXPECT_EQ(bitmap, roaring::Roaring({0, 2}));
}

TEST(DatabaseInsertQueryTest, copiesFilteredRowsFromOneTableIntoAnother) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   database.createTable(TableName{"archive"}, makeValueColumnSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n"
               << R"({"key":"b","country":"US","age":2})" << "\n"
               << R"({"key":"c","country":"CH","age":3})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   const nlohmann::json result = rhydb::test::writeResultToJson(database.executeWrite(
      "source.filter(country='CH').insertInto(archive)", defaultQueryOptions(), "test_request_id"
   ));

   EXPECT_EQ(result.at("insertedRows").get<size_t>(), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "country='CH'"), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "country='US'"), 0);
   EXPECT_EQ(countInTableWhere(database, "archive", "age=1"), 1);
   EXPECT_EQ(countInTableWhere(database, "archive", "age=3"), 1);
   // The source table is left unchanged.
   EXPECT_EQ(countInTableWhere(database, "source", "true"), 3);
}

TEST(DatabaseInsertQueryTest, reshapesWithProjectAndAccumulates) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueColumnSchema());
   // Target keeps only a subset of columns; the query must project down to match it.
   const ColumnIdentifier key{.name = "key", .type = DataType::STRING};
   const ColumnIdentifier country{.name = "country", .type = DataType::STRING};
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

   // project drops the `age` column the target does not have.
   const nlohmann::json result = rhydb::test::writeResultToJson(database.executeWrite(
      "source.project({key, country}).insertInto(archive)", defaultQueryOptions(), "test_request_id"
   ));
   EXPECT_EQ(result.at("insertedRows").get<size_t>(), 2);
   EXPECT_EQ(countInTableWhere(database, "archive", "true"), 2);

   // A second append accumulates on top of the existing rows rather than replacing them.
   const nlohmann::json result_again = rhydb::test::writeResultToJson(database.executeWrite(
      "source.filter(country='US').project({key, country}).insertInto(archive)",
      defaultQueryOptions(),
      "test_request_id"
   ));
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
   const nlohmann::json result = rhydb::test::writeResultToJson(database.executeWrite(
      "source.insertInto(archive)",
      rhydb::config::QueryOptions{.materialization_cutoff = 1},
      "test_request_id"
   ));

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
