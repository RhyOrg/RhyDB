#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

namespace {
const std::string SORTED_DATE_VALUE = "2020-12-24";
const std::string UNSORTED_DATE_VALUE = "2023-01-20";

const nlohmann::json DATA = {
   {"primaryKey", "id"},
   {"sorted_date", SORTED_DATE_VALUE},
   {"unsorted_date", UNSORTED_DATE_VALUE},
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "sorted_date"
      type: "date"
    - name: "unsorted_date"
      type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {},
   {},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = {DATA},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const nlohmann::json EXPECTED_RESULT = {
   {{"primaryKey", "id"}, {"sorted_date", SORTED_DATE_VALUE}, {"unsorted_date", UNSORTED_DATE_VALUE},
   },
};

const QueryTestScenario SORTED_DATE_WITH_TO_AND_FROM_SCENARIO = {
   .name = "SORTED_DATE_WITH_TO_AND_FROM_SCENARIO",
   .query = "default.filter(sorted_date.between('2020-12-24'::date, '2020-12-24'::date))",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario SORTED_DATE_WITH_TO_ONLY_SCENARIO = {
   .name = "SORTED_DATE_WITH_TO_ONLY_SCENARIO",
   .query = "default.filter(sorted_date <= '2020-12-24'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario SORTED_DATE_WITH_FROM_ONLY_SCENARIO = {
   .name = "SORTED_DATE_WITH_FROM_ONLY_SCENARIO",
   .query = "default.filter(sorted_date >= '2020-12-24'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_TO_AND_FROM_SCENARIO = {
   .name = "UNSORTED_DATE_WITH_TO_AND_FROM_SCENARIO",
   .query = "default.filter(unsorted_date.between('2023-01-20'::date, '2023-01-20'::date))",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_TO_ONLY_SCENARIO = {
   .name = "UNSORTED_DATE_WITH_TO_ONLY_SCENARIO",
   .query = "default.filter(unsorted_date <= '2023-01-20'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_FROM_ONLY_SCENARIO = {
   .name = "UNSORTED_DATE_WITH_FROM_ONLY_SCENARIO",
   .query = "default.filter(unsorted_date >= '2023-01-20'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_COLUMN_NOT_IN_DB = {
   .name = "UNSORTED_DATE_WITH_COLUMN_NOT_IN_DB",
   .query = "default.filter(something_not_in_database >= '2000-01-01'::date)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'something_not_in_database' at 1:16",
};

const QueryTestScenario UNSORTED_DATE_WITH_NON_DATE_COLUMN = {
   .name = "UNSORTED_DATE_WITH_NON_DATE_COLUMN",
   .query = "default.filter(primaryKey >= '2020-01-01'::date)",
   .expected_error_message = "The column 'primaryKey' is not of type date",
};

}  // namespace

QUERY_TEST(
   DateBetweenTest,
   TEST_DATA,
   ::testing::Values(
      SORTED_DATE_WITH_TO_AND_FROM_SCENARIO,
      SORTED_DATE_WITH_TO_ONLY_SCENARIO,
      SORTED_DATE_WITH_FROM_ONLY_SCENARIO,
      UNSORTED_DATE_WITH_TO_AND_FROM_SCENARIO,
      UNSORTED_DATE_WITH_TO_ONLY_SCENARIO,
      UNSORTED_DATE_WITH_FROM_ONLY_SCENARIO,
      UNSORTED_DATE_WITH_COLUMN_NOT_IN_DB,
      UNSORTED_DATE_WITH_NON_DATE_COLUMN
   )
);
