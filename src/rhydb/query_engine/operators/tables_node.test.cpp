#include <map>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/database.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"},
    {"country", "CH"},
    {"segment1", nullptr},
    {"gene1", nullptr},
    {"unaligned_segment1", nullptr}},
   {{"primaryKey", "id_1"},
    {"country", "DE"},
    {"segment1", nullptr},
    {"gene1", nullptr},
    {"unaligned_segment1", nullptr}},
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "country"
      type: "string"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ACGT"}},
   {{"gene1", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES
};

const QueryTestScenario TABLES_SCENARIO = {
   .name = "TABLES",
   .query = "tables()",
   .expected_query_result =
      nlohmann::json({{{"tableName", "default"}}, {{"tableName", "reference_genomes"}}}),
};

const QueryTestScenario TABLES_SCHEMA_SCENARIO = {
   .name = "TABLES_SCHEMA",
   .query = "tables().schema()",
   .expected_query_result = nlohmann::json({{{"fieldName", "tableName"}, {"type", "STRING"}}}),
};

const QueryTestScenario TABLES_EXTRA_ARG_ERROR_SCENARIO = {
   .name = "TABLES_EXTRA_ARG_ERROR",
   .query = "tables(default)",
   .expected_error_message = "tables() received too many positional arguments",
};

}  // namespace

QUERY_TEST(
   TablesTest,
   TEST_DATA,
   ::testing::Values(TABLES_SCENARIO, TABLES_SCHEMA_SCENARIO, TABLES_EXTRA_ARG_ERROR_SCENARIO)
);

namespace {
using rhydb::Database;
using rhydb::config::QueryOptions;
using rhydb::query_engine::Planner;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
using rhydb::schema::TableName;
using rhydb::schema::TableSchema;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::StringColumnMetadata;

std::shared_ptr<TableSchema> makeMinimalSchema() {
   const ColumnIdentifier key{.name = "key", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {key, std::make_shared<StringColumnMetadata>(key.name)},
   };
   return std::make_shared<TableSchema>(std::move(column_metadata), key);
}
}  // namespace

TEST(TablesNodeMultiTableTest, listsAndFiltersMultipleTables) {
   Database database;
   database.createTable(TableName{"source"}, makeMinimalSchema());
   database.createTable(TableName{"archive"}, makeMinimalSchema());
   database.createTable(TableName{"backup"}, makeMinimalSchema());

   auto query_plan =
      Planner::planSaneqlQuery("tables()", database.tables, QueryOptions{}, "tables_query");
   ASSERT_EQ(
      rhydb::test::executeQueryToJsonArray(query_plan),
      nlohmann::json::array({
         {{"tableName", "archive"}},
         {{"tableName", "backup"}},
         {{"tableName", "reference_genomes"}},
         {{"tableName", "source"}},
      })
   );

   auto filtered_plan = Planner::planSaneqlQuery(
      "tables().filter(tableName='backup')",
      database.tables,
      QueryOptions{},
      "filtered_tables_query"
   );
   ASSERT_EQ(
      rhydb::test::executeQueryToJsonArray(filtered_plan),
      nlohmann::json::array({
         {{"tableName", "backup"}},
      })
   );
}

TEST(TablesNodeMultiTableTest, listsOnlyBuiltinTablesForNewDatabase) {
   const Database database;

   auto query_plan =
      Planner::planSaneqlQuery("tables()", database.tables, QueryOptions{}, "empty_tables_query");
   ASSERT_EQ(
      rhydb::test::executeQueryToJsonArray(query_plan),
      nlohmann::json::array({
         {{"tableName", "reference_genomes"}},
      })
   );
}
