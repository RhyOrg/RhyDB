#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/database.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/schema/builtin_tables.h"
#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ACGT"}},
   {{"gene1", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = {},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario BASIC = {
   .name = "BASIC",
   .query = "table({{a:=1,b:=2},{3,4}})",
   .expected_query_result = nlohmann::json({
      {{"a", 1}, {"b", 2}},
      {{"a", 3}, {"b", 4}},
   }),
};

const QueryTestScenario SINGLE_ROW = {
   .name = "SINGLE_ROW",
   .query = "table({{a:=1,b:=2}})",
   .expected_query_result = nlohmann::json({
      {{"a", 1}, {"b", 2}},
   }),
};

const QueryTestScenario SINGLE_COLUMN = {
   .name = "SINGLE_COLUMN",
   .query = "table({{a:=1},{2},{3}})",
   .expected_query_result = nlohmann::json({
      {{"a", 1}},
      {{"a", 2}},
      {{"a", 3}},
   }),
};

const QueryTestScenario MIXED_TYPES_PER_COLUMN = {
   .name = "MIXED_TYPES_PER_COLUMN",
   .query = "table({{i:=1,f:=1.5,s:='x',b:=true},{2,2.5,'y',false}})",
   .expected_query_result = nlohmann::json({
      {{"i", 1}, {"f", 1.5}, {"s", "x"}, {"b", true}},
      {{"i", 2}, {"f", 2.5}, {"s", "y"}, {"b", false}},
   }),
};

const QueryTestScenario NULL_CELLS = {
   .name = "NULL_CELLS",
   .query = "table({{a:=1,b:='x'},{2,null},{null,'y'}})",
   .expected_query_result = nlohmann::json({
      {{"a", 1}, {"b", "x"}},
      {{"a", 2}, {"b", nullptr}},
      {{"a", nullptr}, {"b", "y"}},
   }),
};

const QueryTestScenario DATE_COLUMN = {
   .name = "DATE_COLUMN",
   .query = "table({{d:='2023-01-01'::date},{'2024-12-31'::date}})",
   .expected_query_result = nlohmann::json({
      {{"d", "2023-01-01"}},
      {{"d", "2024-12-31"}},
   }),
};

const QueryTestScenario CONSTANT_EXPRESSION_CELL = {
   .name = "CONSTANT_EXPRESSION_CELL",
   .query = "table({{s:=concat('a','b')},{'c'}})",
   .expected_query_result = nlohmann::json({
      {{"s", "ab"}},
      {{"s", "c"}},
   }),
};

const QueryTestScenario FILTER_ON_LITERAL = {
   .name = "FILTER_ON_LITERAL",
   .query = "table({{a:=1,b:=2},{3,4}}).filter(a=3)",
   .expected_query_result = nlohmann::json({
      {{"a", 3}, {"b", 4}},
   }),
};

const QueryTestScenario MAP_ON_LITERAL = {
   .name = "MAP_ON_LITERAL",
   .query = "table({{a:=1},{2}}).map({doubled := a})",
   .expected_query_result = nlohmann::json({
      {{"a", 1}, {"doubled", 1}},
      {{"a", 2}, {"doubled", 2}},
   }),
};

const QueryTestScenario SCHEMA_OF_LITERAL = {
   .name = "SCHEMA_OF_LITERAL",
   .query = "table({{a:=1,b:='x'}}).schema()",
   .expected_query_result = nlohmann::json({
      {{"fieldName", "a"}, {"type", "INT64"}},
      {{"fieldName", "b"}, {"type", "STRING"}},
   }),
};

const QueryTestScenario UNIONALL_OF_LITERALS = {
   .name = "UNIONALL_OF_LITERALS",
   .query = "table({{a:=1,b:=2}}).unionall(table({{a:=3,b:=4}})).order({a})",
   .expected_query_result = nlohmann::json({
      {{"a", 1}, {"b", 2}},
      {{"a", 3}, {"b", 4}},
   }),
};

const QueryTestScenario NOT_A_SET_ERROR = {
   .name = "NOT_A_SET_ERROR",
   .query = "table(1)",
   .expected_error_message = "table() expects a set of rows like {a:=1,b:=2},{3,4} at 1:7",
};

const QueryTestScenario EMPTY_SET_ERROR = {
   .name = "EMPTY_SET_ERROR",
   .query = "table({})",
   .expected_error_message = "table() requires at least one row at 1:7",
};

const QueryTestScenario FIRST_ROW_POSITIONAL_ERROR = {
   .name = "FIRST_ROW_POSITIONAL_ERROR",
   .query = "table({{1,2},{3,4}})",
   .expected_error_message = "table()'s first row must name its columns like {a:=1,b:=2} at 1:8",
};

const QueryTestScenario NAMED_LATER_ROW_ERROR = {
   .name = "NAMED_LATER_ROW_ERROR",
   .query = "table({{a:=1,b:=2},{a:=3,b:=4}})",
   .expected_error_message = "table() rows after the first must be positional like {3,4} at 1:20",
};

const QueryTestScenario DUPLICATE_NAME_ERROR = {
   .name = "DUPLICATE_NAME_ERROR",
   .query = "table({{a:=1,a:=2}})",
   .expected_error_message = "table() names the column 'a' more than once at 1:8",
};

const QueryTestScenario WRONG_ROW_WIDTH_ERROR = {
   .name = "WRONG_ROW_WIDTH_ERROR",
   .query = "table({{a:=1,b:=2},{3}})",
   .expected_error_message = "table() row has 1 values but the table has 2 columns at 1:20",
};

const QueryTestScenario MIXED_TYPES_ERROR = {
   .name = "MIXED_TYPES_ERROR",
   .query = "table({{a:=1},{'x'}})",
   .expected_error_message =
      "table() column 'a' mixes values of type 'INT64' and 'STRING' at 1:16; every value in a "
      "column must have the same type",
};

const QueryTestScenario ALL_NULL_COLUMN_ERROR = {
   .name = "ALL_NULL_COLUMN_ERROR",
   .query = "table({{a:=null},{null}})",
   .expected_error_message =
      "table() column 'a' has only null values, so its type cannot be inferred; give at least one "
      "non-null value in the column",
};

const QueryTestScenario COLUMN_REFERENCE_ERROR = {
   .name = "COLUMN_REFERENCE_ERROR",
   .query = "table({{a:=primaryKey}})",
   .expected_error_message =
      "table() cell for column 'a' references unknown column 'primaryKey' at 1:12",
};

}  // namespace

QUERY_TEST(
   TableLiteralTest,
   TEST_DATA,
   ::testing::Values(
      BASIC,
      SINGLE_ROW,
      SINGLE_COLUMN,
      MIXED_TYPES_PER_COLUMN,
      NULL_CELLS,
      DATE_COLUMN,
      CONSTANT_EXPRESSION_CELL,
      FILTER_ON_LITERAL,
      MAP_ON_LITERAL,
      SCHEMA_OF_LITERAL,
      UNIONALL_OF_LITERALS,
      NOT_A_SET_ERROR,
      EMPTY_SET_ERROR,
      FIRST_ROW_POSITIONAL_ERROR,
      NAMED_LATER_ROW_ERROR,
      DUPLICATE_NAME_ERROR,
      WRONG_ROW_WIDTH_ERROR,
      MIXED_TYPES_ERROR,
      ALL_NULL_COLUMN_ERROR,
      COLUMN_REFERENCE_ERROR
   )
);

namespace {
using rhydb::Database;
using rhydb::config::QueryOptions;

nlohmann::json executeWrite(Database& database, const std::string& statement) {
   return rhydb::test::writeResultToJson(
      database.executeWrite(statement, QueryOptions{}, "test_request_id")
   );
}

nlohmann::json executeQuery(Database& database, const std::string& query) {
   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      query, database.tables, QueryOptions{}, "test_request_id"
   );
   return rhydb::test::executeQueryToJsonArray(query_plan);
}
}  // namespace

TEST(TableLiteralInsertTest, insertsLiteralRowsIntoInt32Column) {
   Database database;
   std::ignore = executeWrite(database, "createTable(data, {id := string, age := int})");

   // int literals are INT64; inserting them into an INT32 column round-trips through NDJSON.
   const auto summary =
      executeWrite(database, R"(table({{id:='a',age:=1},{'b',2}}).insertInto(data))");
   EXPECT_EQ(summary, nlohmann::json({{"insertedRows", 2}}));

   EXPECT_EQ(
      executeQuery(database, "data.order({id})"),
      nlohmann::json::array({
         {{"id", "a"}, {"age", 1}},
         {{"id", "b"}, {"age", 2}},
      })
   );
}
