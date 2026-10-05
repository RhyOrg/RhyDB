#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const nlohmann::json MAIN_SEQUENCE = {
   {"sequence", "ATCG"},
   {"insertions", nlohmann::json::array()}
};

// Per country: Switzerland has two complete rows, Germany one complete row and one row that is null
// in every numeric column, France only such a null row, and one row has no country at all. `reads`
// exceeds the int32 range, and the float values are exactly representable so their sums print
// without rounding ambiguity.
const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"},
    {"country", "Switzerland"},
    {"age", 5},
    {"reads", 3000000000},
    {"coverage", 0.5},
    {"main", MAIN_SEQUENCE}},
   {{"primaryKey", "id_1"},
    {"country", "Switzerland"},
    {"age", 7},
    {"reads", 3000000000},
    {"coverage", 1.25},
    {"main", MAIN_SEQUENCE}},
   {{"primaryKey", "id_2"},
    {"country", "Germany"},
    {"age", nullptr},
    {"reads", nullptr},
    {"coverage", nullptr},
    {"main", MAIN_SEQUENCE}},
   {{"primaryKey", "id_3"},
    {"country", "Germany"},
    {"age", 10},
    {"reads", 1},
    {"coverage", 2.0},
    {"main", MAIN_SEQUENCE}},
   {{"primaryKey", "id_4"},
    {"country", nullptr},
    {"age", 1},
    {"reads", 2},
    {"coverage", 0.25},
    {"main", MAIN_SEQUENCE}},
   {{"primaryKey", "id_5"},
    {"country", "France"},
    {"age", nullptr},
    {"reads", nullptr},
    {"coverage", nullptr},
    {"main", MAIN_SEQUENCE}},
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
   .reference_genomes = ReferenceGenomes{{{"main", "ATCG"}}, {}},
   .without_unaligned_sequences = true,
};

const QueryTestScenario SUM_WITHOUT_GROUPS = {
   .name = "SUM_WITHOUT_GROUPS",
   .query = "default.group(by:={}, aggs:={total:=sum(age)})",
   .expected_query_result = nlohmann::json::parse(R"([{"total": 23}])")
};

// Null values are skipped; France has no non-null age, so its sum is null. The row without a
// country forms its own group, which the ascending order lists first.
const QueryTestScenario SUM_INT32_PER_GROUP = {
   .name = "SUM_INT32_PER_GROUP",
   .query = "default.group(by:={country}, aggs:={total:=sum(age)}).order(by:={country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "total": 1},
      {"country": "France", "total": null},
      {"country": "Germany", "total": 10},
      {"country": "Switzerland", "total": 12}
   ])")
};

const QueryTestScenario SUM_INT64_PER_GROUP = {
   .name = "SUM_INT64_PER_GROUP",
   .query = "default.group(by:={country}, aggs:={total:=sum(reads)}).order(by:={country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "total": 2},
      {"country": "France", "total": null},
      {"country": "Germany", "total": 1},
      {"country": "Switzerland", "total": 6000000000}
   ])")
};

const QueryTestScenario SUM_FLOAT_PER_GROUP = {
   .name = "SUM_FLOAT_PER_GROUP",
   .query = "default.group(by:={country}, aggs:={total:=sum(coverage)}).order(by:={country})",
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
      "default.group(by:={country}, aggs:={count:=count(), age_total:=sum(age), "
      "reads_total:=sum(reads)})"
      ".order(by:={country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "count": 1, "age_total": 1, "reads_total": 2},
      {"country": "France", "count": 1, "age_total": null, "reads_total": null},
      {"country": "Germany", "count": 2, "age_total": 10, "reads_total": 1},
      {"country": "Switzerland", "count": 2, "age_total": 12, "reads_total": 6000000000}
   ])")
};

const QueryTestScenario SUM_OF_MAPPED_COLUMN = {
   .name = "SUM_OF_MAPPED_COLUMN",
   .query = "default.map({years := age}).group(by:={}, aggs:={total:=sum(years)})",
   .expected_query_result = nlohmann::json::parse(R"([{"total": 23}])")
};

// Without `by` columns there is always one result row; with no rows to sum it holds null (while
// count() is 0).
const QueryTestScenario SUM_OVER_NO_ROWS = {
   .name = "SUM_OVER_NO_ROWS",
   .query =
      "default.filter(country = 'Italy').group(by:={}, aggs:={count:=count(), total:=sum(age)})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 0, "total": null}])")
};

const QueryTestScenario SUM_OVER_NO_ROWS_PER_GROUP = {
   .name = "SUM_OVER_NO_ROWS_PER_GROUP",
   .query = "default.filter(country = 'Italy').group(by:={country}, aggs:={total:=sum(age)})",
   .expected_query_result = nlohmann::json::array()
};

const QueryTestScenario SUM_OF_STRING_COLUMN = {
   .name = "SUM_OF_STRING_COLUMN",
   .query = "default.group(by:={}, aggs:={total:=sum(country)})",
   .expected_error_message =
      "aggregate 'total': sum requires a numeric (int, int64 or float) column, but 'country' has "
      "type STRING"
};

const QueryTestScenario SUM_WITHOUT_COLUMN = {
   .name = "SUM_WITHOUT_COLUMN",
   .query = "default.group(by:={}, aggs:={total:=sum()})",
   .expected_error_message =
      "aggregate 'total': sum expects exactly one column argument, e.g. sum(age)"
};

const QueryTestScenario SUM_OF_TWO_COLUMNS = {
   .name = "SUM_OF_TWO_COLUMNS",
   .query = "default.group(by:={}, aggs:={total:=sum(age, reads)})",
   .expected_error_message =
      "aggregate 'total': sum expects exactly one column argument, e.g. sum(age)"
};

const QueryTestScenario SUM_OF_UNKNOWN_COLUMN = {
   .name = "SUM_OF_UNKNOWN_COLUMN",
   .query = "default.group(by:={}, aggs:={total:=sum(weight)})",
   .expected_error_message = "source column weight is not present in the input's output schema"
};

// executes a full count(*) on a non-collapsible table scan (unionAll of two identical tables)
// makes sure that an "empty" table scan executes correctly
// (since the group by requires no columns, no columns should be read by the table scan)
const QueryTestScenario COUNT_NON_COLLAPSIBLE_TABLE_SCAN = {
   .name = "COUNT_NON_COLLAPSIBLE_TABLE_SCAN",
   .query =
      "unionall(reference_genomes, reference_genomes).group(by:={}, aggs:={count := count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 2}])"),
};

const QueryTestScenario COUNT_STAR_WITH_CUSTOM_NAME = {
   .name = "COUNT_STAR_WITH_CUSTOM_NAME",
   .query = "default.group(by:={}, aggs:={n := count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"n": 6}])")
};

const QueryTestScenario COUNT_PER_GROUP_WITH_CUSTOM_NAME = {
   .name = "COUNT_PER_GROUP_WITH_CUSTOM_NAME",
   .query = "default.group(by:={country}, aggs:={n := count()}).order(by:={country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "n": 1},
      {"country": "France", "n": 1},
      {"country": "Germany", "n": 2},
      {"country": "Switzerland", "n": 2}
   ])")
};

// count(column) only counts the rows whose value in the column is not null, unlike count()
const QueryTestScenario COUNT_COLUMN_PER_GROUP = {
   .name = "COUNT_COLUMN_PER_GROUP",
   .query =
      "default.group(by:={country}, aggs:={n:=count(), with_age:=count(age), "
      "with_coverage:=count(coverage)}).order(by:={country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "n": 1, "with_age": 1, "with_coverage": 1},
      {"country": "France", "n": 1, "with_age": 0, "with_coverage": 0},
      {"country": "Germany", "n": 2, "with_age": 1, "with_coverage": 1},
      {"country": "Switzerland", "n": 2, "with_age": 2, "with_coverage": 2}
   ])")
};

const QueryTestScenario COUNT_COLUMN_WITHOUT_GROUPS = {
   .name = "COUNT_COLUMN_WITHOUT_GROUPS",
   .query = "default.group(by:={}, aggs:={with_country:=count(country), with_reads:=count(reads)})",
   .expected_query_result = nlohmann::json::parse(R"([{"with_country": 5, "with_reads": 4}])")
};

const QueryTestScenario COUNT_COLUMN_OVER_NO_ROWS = {
   .name = "COUNT_COLUMN_OVER_NO_ROWS",
   .query = "default.filter(country = 'Italy').group(by:={}, aggs:={with_age:=count(age)})",
   .expected_query_result = nlohmann::json::parse(R"([{"with_age": 0}])")
};

const QueryTestScenario COUNT_OF_TWO_COLUMNS = {
   .name = "COUNT_OF_TWO_COLUMNS",
   .query = "default.group(by:={}, aggs:={n:=count(age, reads)})",
   .expected_error_message =
      "aggregate 'n': count expects no argument or exactly one column argument, e.g. count() or "
      "count(age)"
};

const QueryTestScenario COUNT_OF_UNKNOWN_COLUMN = {
   .name = "COUNT_OF_UNKNOWN_COLUMN",
   .query = "default.group(by:={}, aggs:={n:=count(weight)})",
   .expected_error_message = "source column weight is not present in the input's output schema"
};

// Null values are skipped; France has no non-null value, so its extrema are null
const QueryTestScenario MIN_MAX_PER_GROUP = {
   .name = "MIN_MAX_PER_GROUP",
   .query =
      "default.group(by:={country}, aggs:={youngest:=min(age), oldest:=max(age), "
      "least_reads:=min(reads), max_coverage:=max(coverage)}).order(by:={country})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"country": null, "youngest": 1, "oldest": 1, "least_reads": 2, "max_coverage": 0.25},
      {"country": "France", "youngest": null, "oldest": null, "least_reads": null, "max_coverage": null},
      {"country": "Germany", "youngest": 10, "oldest": 10, "least_reads": 1, "max_coverage": 2.0},
      {"country": "Switzerland", "youngest": 5, "oldest": 7, "least_reads": 3000000000, "max_coverage": 1.25}
   ])")
};

const QueryTestScenario MIN_MAX_OF_STRING_COLUMN = {
   .name = "MIN_MAX_OF_STRING_COLUMN",
   .query = "default.group(by:={}, aggs:={first:=min(country), last:=max(country)})",
   .expected_query_result = nlohmann::json::parse(R"([{"first": "France", "last": "Switzerland"}])")
};

const QueryTestScenario MAX_WITHOUT_COLUMN = {
   .name = "MAX_WITHOUT_COLUMN",
   .query = "default.group(by:={}, aggs:={oldest:=max()})",
   .expected_error_message =
      "aggregate 'oldest': max expects exactly one column argument, e.g. max(age)"
};

}  // namespace

QUERY_TEST(
   AggregateMinMax,
   TEST_DATA,
   ::testing::Values(MIN_MAX_PER_GROUP, MIN_MAX_OF_STRING_COLUMN, MAX_WITHOUT_COLUMN)
);

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
      COUNT_COLUMN_PER_GROUP,
      COUNT_COLUMN_WITHOUT_GROUPS,
      COUNT_COLUMN_OVER_NO_ROWS,
      COUNT_OF_TWO_COLUMNS,
      COUNT_OF_UNKNOWN_COLUMN
   )
);
