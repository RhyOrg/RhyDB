#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

// Per country: Switzerland has two complete rows, Germany one complete row and one row that is null
// in every numeric column, France only such a null row, and one row has no country at all. `reads`
// exceeds the int32 range, and the float values are exactly representable so their sums print
// without rounding ambiguity.
const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"},
    {"country", "Switzerland"},
    {"age", 5},
    {"reads", 3000000000},
    {"coverage", 0.5}},
   {{"primaryKey", "id_1"},
    {"country", "Switzerland"},
    {"age", 7},
    {"reads", 3000000000},
    {"coverage", 1.25}},
   {{"primaryKey", "id_2"},
    {"country", "Germany"},
    {"age", nullptr},
    {"reads", nullptr},
    {"coverage", nullptr}},
   {{"primaryKey", "id_3"}, {"country", "Germany"}, {"age", 10}, {"reads", 1}, {"coverage", 2.0}},
   {{"primaryKey", "id_4"}, {"country", nullptr}, {"age", 1}, {"reads", 2}, {"coverage", 0.25}},
   {{"primaryKey", "id_5"},
    {"country", "France"},
    {"age", nullptr},
    {"reads", nullptr},
    {"coverage", nullptr}},
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
    - name: "age"
      type: "int"
    - name: "reads"
      type: "int64"
    - name: "coverage"
      type: "float"
  primaryKey: "primaryKey"
)";

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = ReferenceGenomes{
      {{"main", "ATCG"}}, {}},
};

const QueryTestScenario SUM_WITHOUT_GROUPS = {
   .name = "SUM_WITHOUT_GROUPS",
   .query = "default.groupBy({total:=sum(age)})",
   .expected_query_result = nlohmann::json::parse(R"([{"total": 23}])")
};

// Null values are skipped; France has no non-null age, so its sum is null. The row without a
// country forms its own group, which the ascending order lists first.
const QueryTestScenario SUM_INT32_PER_GROUP = {
   .name = "SUM_INT32_PER_GROUP",
   .query = "default.groupBy({total:=sum(age)}, {country}).orderBy({country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "total": 1},
      {"country": "France", "total": null},
      {"country": "Germany", "total": 10},
      {"country": "Switzerland", "total": 12}
   ])")
};

const QueryTestScenario SUM_INT64_PER_GROUP = {
   .name = "SUM_INT64_PER_GROUP",
   .query = "default.groupBy({total:=sum(reads)}, {country}).orderBy({country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "total": 2},
      {"country": "France", "total": null},
      {"country": "Germany", "total": 1},
      {"country": "Switzerland", "total": 6000000000}
   ])")
};

const QueryTestScenario SUM_FLOAT_PER_GROUP = {
   .name = "SUM_FLOAT_PER_GROUP",
   .query = "default.groupBy({total:=sum(coverage)}, {country}).orderBy({country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "total": 0.25},
      {"country": "France", "total": null},
      {"country": "Germany", "total": 2.0},
      {"country": "Switzerland", "total": 1.75}
   ])")
};

// count() counts every row of the group, including those whose summed column is null.
const QueryTestScenario SUM_AND_COUNT_TOGETHER = {
   .name = "SUM_AND_COUNT_TOGETHER",
   .query =
      "default.groupBy({count:=count(), age_total:=sum(age), reads_total:=sum(reads)}, {country})"
      ".orderBy({country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "count": 1, "age_total": 1, "reads_total": 2},
      {"country": "France", "count": 1, "age_total": null, "reads_total": null},
      {"country": "Germany", "count": 2, "age_total": 10, "reads_total": 1},
      {"country": "Switzerland", "count": 2, "age_total": 12, "reads_total": 6000000000}
   ])")
};

const QueryTestScenario SUM_OF_MAPPED_COLUMN = {
   .name = "SUM_OF_MAPPED_COLUMN",
   .query = "default.map({years := age}).groupBy({total:=sum(years)})",
   .expected_query_result = nlohmann::json::parse(R"([{"total": 23}])")
};

// Without groupBy columns there is always one result row; with no rows to sum it holds null (while
// count() is 0).
const QueryTestScenario SUM_OVER_NO_ROWS = {
   .name = "SUM_OVER_NO_ROWS",
   .query = "default.filter(country = 'Italy').groupBy({count:=count(), total:=sum(age)})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 0, "total": null}])")
};

const QueryTestScenario SUM_OVER_NO_ROWS_PER_GROUP = {
   .name = "SUM_OVER_NO_ROWS_PER_GROUP",
   .query = "default.filter(country = 'Italy').groupBy({total:=sum(age)}, {country})",
   .expected_query_result = nlohmann::json::array()
};

const QueryTestScenario SUM_OF_STRING_COLUMN = {
   .name = "SUM_OF_STRING_COLUMN",
   .query = "default.groupBy({total:=sum(country)})",
   .expected_error_message =
      "aggregate 'total': sum requires a numeric (int, int64 or float) column, but 'country' has "
      "type STRING"
};

const QueryTestScenario SUM_WITHOUT_COLUMN = {
   .name = "SUM_WITHOUT_COLUMN",
   .query = "default.groupBy({total:=sum()})",
   .expected_error_message =
      "aggregate 'total': sum expects exactly one column argument, e.g. sum(age)"
};

const QueryTestScenario SUM_OF_TWO_COLUMNS = {
   .name = "SUM_OF_TWO_COLUMNS",
   .query = "default.groupBy({total:=sum(age, reads)})",
   .expected_error_message =
      "aggregate 'total': sum expects exactly one column argument, e.g. sum(age)"
};

const QueryTestScenario SUM_OF_UNKNOWN_COLUMN = {
   .name = "SUM_OF_UNKNOWN_COLUMN",
   .query = "default.groupBy({total:=sum(weight)})",
   .expected_error_message = "source column weight is not present in the input's output schema"
};

// executes a full count(*) on a non-collapsible table scan (unionAll of two identical tables)
// makes sure that an "empty" table scan executes correctly
// (since the group by requires no columns, no columns should be read by the table scan)
const QueryTestScenario COUNT_NON_COLLAPSIBLE_TABLE_SCAN = {
   .name = "COUNT_NON_COLLAPSIBLE_TABLE_SCAN",
   .query = "unionAll(reference_genomes, reference_genomes).groupBy({count := count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 2}])"),
};

const QueryTestScenario COUNT_STAR_WITH_CUSTOM_NAME = {
   .name = "COUNT_STAR_WITH_CUSTOM_NAME",
   .query = "default.groupBy({n := count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"n": 6}])")
};

const QueryTestScenario COUNT_PER_GROUP_WITH_CUSTOM_NAME = {
   .name = "COUNT_PER_GROUP_WITH_CUSTOM_NAME",
   .query = "default.groupBy({n := count()}, {country}).orderBy({country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "n": 1},
      {"country": "France", "n": 1},
      {"country": "Germany", "n": 2},
      {"country": "Switzerland", "n": 2}
   ])")
};

// count(<column>) is not yet implemented. Without grouping keys this used to be silently rewritten
// into a full count(*) (returning the wrong result); it must surface the same error the grouped
// case already gives.
const QueryTestScenario COUNT_OF_COLUMN_WITHOUT_GROUPS = {
   .name = "COUNT_OF_COLUMN_WITHOUT_GROUPS",
   .query = "default.groupBy({n := count(country)})",
   .expected_error_message = "count(<column_ref>) not yet implemented"
};

}  // namespace

QUERY_TEST(
   AggregateSum,
   TEST_DATA,
   ::testing::Values(
      SUM_WITHOUT_GROUPS,
      SUM_INT32_PER_GROUP,
      SUM_INT64_PER_GROUP,
      SUM_FLOAT_PER_GROUP,
      SUM_AND_COUNT_TOGETHER,
      SUM_OF_MAPPED_COLUMN,
      SUM_OVER_NO_ROWS,
      SUM_OVER_NO_ROWS_PER_GROUP,
      SUM_OF_STRING_COLUMN,
      SUM_WITHOUT_COLUMN,
      SUM_OF_TWO_COLUMNS,
      SUM_OF_UNKNOWN_COLUMN,
      COUNT_NON_COLLAPSIBLE_TABLE_SCAN
   )
);

QUERY_TEST(
   AggregateCount,
   TEST_DATA,
   ::testing::Values(
      COUNT_STAR_WITH_CUSTOM_NAME,
      COUNT_PER_GROUP_WITH_CUSTOM_NAME,
      COUNT_OF_COLUMN_WITHOUT_GROUPS
   )
);
