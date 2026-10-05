#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const std::string& country,
   const std::string& region,
   int32_t year,
   double score,
   const std::string& date
) {
   return {
      {"primaryKey", primary_key},
      {"country", country},
      {"region", region},
      {"year", year},
      {"score", score},
      {"date", date}
   };
}

// `country` is dictionary-encoded, `region` a plain string, `year` an int, `score` a float, `date`
// a date.
const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "test"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "country"
     type: "string"
     generateIndex: true
   - name: "region"
     type: "string"
   - name: "year"
     type: "int"
   - name: "score"
     type: "float"
   - name: "date"
     type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_0", "Germany", "Europe", 2020, 1e-7, "2020-01-01"),
         createData("id_1", "France", "Europe", 2021, 2e-7, "2021-06-01"),
         createData("id_2", "Japan", "Asia", 2022, 3e-7, "2022-03-15"),
         createData("id_3", "Germany", "Europe", 2020, 0.5, "2020-01-01"),
         createData("id_4", "Brazil", "SouthAmerica", 2023, 1.0, "2023-12-31"),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES
};

const QueryTestScenario IN_SET_LITERAL_STRING = {
   .name = "IN_SET_LITERAL_STRING",
   .query = "default.filter(country.in({'Japan', 'Brazil'})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_2"},{"primaryKey":"id_4"}])")
};

// A set literal of integers on an int column.
const QueryTestScenario IN_SET_LITERAL_INT_COLUMN = {
   .name = "IN_SET_LITERAL_INT_COLUMN",
   .query = "default.filter(year.in({2020, 2023})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"},{"primaryKey":"id_4"}])"
      )
};

// A set literal of floats on a float column.
const QueryTestScenario IN_SET_LITERAL_FLOAT_COLUMN = {
   .name = "IN_SET_LITERAL_FLOAT_COLUMN",
   .query = "default.filter(score.in({0.0000002, 0.5})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_1"},{"primaryKey":"id_3"}])")
};

const QueryTestScenario IN_SET_LITERAL_DATE_COLUMN = {
   .name = "IN_SET_LITERAL_DATE_COLUMN",
   .query =
      "default.filter(date.in({'2021-06-01'::date, '2023-12-31'::date})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_1"},{"primaryKey":"id_4"}])")
};

const QueryTestScenario IN_DUPLICATE_VALUES = {
   .name = "IN_DUPLICATE_VALUES",
   .query = "default.filter(country.in({'Japan', 'Japan'})).project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_2"}])")
};

const QueryTestScenario IN_NEGATED_INT_COLUMN = {
   .name = "IN_NEGATED_INT_COLUMN",
   .query = "default.filter(!year.in({2020, 2023})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_1"},{"primaryKey":"id_2"}])")
};

// Every value is compared against the column's type, so one mismatching value fails the query.
const QueryTestScenario IN_MIXED_TYPES = {
   .name = "IN_MIXED_TYPES",
   .query = "default.filter(country.in({2025, 'Germany'})).project({primaryKey})",
   .expected_error_message = "The column 'country' is not of type int"
};

const QueryTestScenario IN_STRING_VALUE_ON_INT_COLUMN = {
   .name = "IN_STRING_VALUE_ON_INT_COLUMN",
   .query = "default.filter(year.in({'2020'})).project({primaryKey})",
   .expected_error_message = "The column 'year' is not of type string"
};

const QueryTestScenario IN_INT_OUT_OF_RANGE = {
   .name = "IN_INT_OUT_OF_RANGE",
   .query = "default.filter(year.in({3000000000})).project({primaryKey})",
   .expected_error_message = "Cannot cast 3000000000 to int32. Value out of range"
};

const QueryTestScenario IN_NESTED_SET_LITERAL = {
   .name = "IN_NESTED_SET_LITERAL",
   .query = "default.filter(country.in({{'Germany'}})).project({primaryKey})",
   .expected_error_message =
      "in() value must be a literal value (int, float, string, bool, or date), a column reference, "
      "or a scalar function call at 1:28"
};

const QueryTestScenario IN_COMPARISON_AS_VALUE = {
   .name = "IN_COMPARISON_AS_VALUE",
   .query = "default.filter(country.in({region = 'Europe'})).project({primaryKey})",
   .expected_error_message =
      "in() value must be a literal value (int, float, string, bool, or date), a column reference, "
      "or a scalar function call at 1:35"
};

// A column reference parses as a value, but `country = region` cannot be compiled to a filter.
const QueryTestScenario IN_COLUMN_REFERENCE_AS_VALUE = {
   .name = "IN_COLUMN_REFERENCE_AS_VALUE",
   .query = "default.filter(country.in({region})).project({primaryKey})",
   .expected_error_message =
      "A Comparison expression can only be compiled to a filter when exactly one side is a column "
      "reference and the other a literal value"
};

const QueryTestScenario IN_UNKNOWN_COLUMN_REFERENCE_AS_VALUE = {
   .name = "IN_UNKNOWN_COLUMN_REFERENCE_AS_VALUE",
   .query = "default.filter(country.in({doesNotExist})).project({primaryKey})",
   .expected_error_message = "in() value references unknown column 'doesNotExist' at 1:28"
};

// The column is resolved even when the set is empty and the filter would be `false`.
const QueryTestScenario IN_NONEXISTENT_COLUMN_EMPTY_SET = {
   .name = "IN_NONEXISTENT_COLUMN_EMPTY_SET",
   .query = "default.filter(doesNotExist.in({})).project({primaryKey})",
   .expected_error_message = "The database does not contain the column 'doesNotExist'"
};

}  // namespace

QUERY_TEST(
   InTest,
   TEST_DATA,
   ::testing::Values(
      IN_SET_LITERAL_STRING,
      IN_SET_LITERAL_INT_COLUMN,
      IN_SET_LITERAL_FLOAT_COLUMN,
      IN_SET_LITERAL_DATE_COLUMN,
      IN_DUPLICATE_VALUES,
      IN_NEGATED_INT_COLUMN,
      IN_MIXED_TYPES,
      IN_STRING_VALUE_ON_INT_COLUMN,
      IN_INT_OUT_OF_RANGE,
      IN_NESTED_SET_LITERAL,
      IN_COMPARISON_AS_VALUE,
      IN_COLUMN_REFERENCE_AS_VALUE,
      IN_UNKNOWN_COLUMN_REFERENCE_AS_VALUE,
      IN_NONEXISTENT_COLUMN_EMPTY_SET
   )
)
