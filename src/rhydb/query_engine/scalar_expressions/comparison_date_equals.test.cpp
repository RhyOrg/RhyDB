#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

namespace {
const std::string DATE_2020 = "2020-12-24";
const std::string DATE_2021 = "2021-06-15";
const std::string DATE_2023 = "2023-01-20";

const nlohmann::json DATA_ROW1 = {
   {"primaryKey", "row1"},
   {"sorted_date", DATE_2020},
   {"unsorted_date", DATE_2023},
};

const nlohmann::json DATA_ROW2 = {
   {"primaryKey", "row2"},
   {"sorted_date", DATE_2021},
   {"unsorted_date", DATE_2020},
};

const nlohmann::json DATA_ROW3 = {
   {"primaryKey", "row3"},
   {"sorted_date", DATE_2020},
   {"unsorted_date", DATE_2021},
};

const nlohmann::json DATA_NULL1 = {
   {"primaryKey", "null1"},
   {"sorted_date", nullptr},
   {"unsorted_date", nullptr},
};

const nlohmann::json DATA_NULL2 = {
   {"primaryKey", "null2"},
   {"sorted_date", nullptr},
   {"unsorted_date", DATE_2023},
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
   .ndjson_input_data = {DATA_ROW1, DATA_ROW2, DATA_ROW3, DATA_NULL1, DATA_NULL2},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

std::string createDateEqualsQuery(const std::string& column, const std::string& date_value) {
   return fmt::format("default.filter({} = '{}'::date)", column, date_value);
}

std::string createDateEqualsNullQuery(const std::string& column) {
   return fmt::format("default.filter({} = null)", column);
}

// Matches row1 and row3 (both have sorted_date = 2020-12-24)
const QueryTestScenario SORTED_DATE_MULTIPLE_MATCHES =
   {.name = "SORTED_DATE_MULTIPLE_MATCHES",
    .query = createDateEqualsQuery("sorted_date", DATE_2020),
    .expected_query_result = {
       {{"primaryKey", "row1"}, {"sorted_date", DATE_2020}, {"unsorted_date", DATE_2023}},
       {{"primaryKey", "row3"}, {"sorted_date", DATE_2020}, {"unsorted_date", DATE_2021}},
    },};

// Matches only row2 (sorted_date = 2021-06-15)
const QueryTestScenario SORTED_DATE_SINGLE_MATCH =
   {.name = "SORTED_DATE_SINGLE_MATCH",
    .query = createDateEqualsQuery("sorted_date", DATE_2021),
    .expected_query_result = {
       {{"primaryKey", "row2"}, {"sorted_date", DATE_2021}, {"unsorted_date", DATE_2020}},
    },};

// Matches row1 and null2 (both have unsorted_date = 2023-01-20)
const QueryTestScenario UNSORTED_DATE_MULTIPLE_MATCHES =
   {.name = "UNSORTED_DATE_MULTIPLE_MATCHES",
    .query = createDateEqualsQuery("unsorted_date", DATE_2023),
    .expected_query_result = {
       {{"primaryKey", "row1"}, {"sorted_date", DATE_2020}, {"unsorted_date", DATE_2023}},
       {{"primaryKey", "null2"}, {"sorted_date", nullptr}, {"unsorted_date", DATE_2023}},
    },};

// Matches only row2 (unsorted_date = 2020-12-24)
const QueryTestScenario UNSORTED_DATE_SINGLE_MATCH =
   {.name = "UNSORTED_DATE_SINGLE_MATCH",
    .query = createDateEqualsQuery("unsorted_date", DATE_2020),
    .expected_query_result = {
       {{"primaryKey", "row2"}, {"sorted_date", DATE_2021}, {"unsorted_date", DATE_2020}},
    },};

// `... = null` is rejected; users must use isNull()
const QueryTestScenario SORTED_DATE_NULL_REJECTED = {
   .name = "SORTED_DATE_NULL_REJECTED",
   .query = createDateEqualsNullQuery("sorted_date"),
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:30",
};

const QueryTestScenario UNSORTED_DATE_NULL_REJECTED = {
   .name = "UNSORTED_DATE_NULL_REJECTED",
   .query = createDateEqualsNullQuery("unsorted_date"),
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:32",
};

const QueryTestScenario DATE_EQUALS_NO_MATCH = {
   .name = "DATE_EQUALS_NO_MATCH",
   .query = createDateEqualsQuery("sorted_date", "1999-01-01"),
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario DATE_EQUALS_WRONG_FORMAT = {
   .name = "DATE_EQUALS_WRONG_FORMAT",
   .query = "default.filter(sorted_date = '2021-03-00018'::date)",
   .expected_error_message =
      "invalid date '2021-03-00018' at 1:45: Invalid date format '2021-03-00018': "
      "expected exactly YYYY-MM-DD",
};

const QueryTestScenario DATE_EQUALS_WRONG_VALUE_TYPE = {
   .name = "DATE_EQUALS_WRONG_VALUE_TYPE",
   .query = "default.filter(sorted_date = 'asdf'::date)",
   .expected_error_message =
      "invalid date 'asdf' at 1:36: Invalid date format 'asdf': expected exactly YYYY-MM-DD",
};

const QueryTestScenario DATE_EQUALS_COLUMN_NOT_IN_DB = {
   .name = "DATE_EQUALS_COLUMN_NOT_IN_DB",
   .query = "default.filter(something_not_in_database = '2020-01-01'::date)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'something_not_in_database' at 1:16",
};

const QueryTestScenario DATE_EQUALS_WRONG_COLUMN_TYPE = {
   .name = "DATE_EQUALS_WRONG_COLUMN_TYPE",
   .query = "default.filter(primaryKey = '2020-01-01'::date)",
   .expected_error_message = "The column 'primaryKey' is not of type date",
};

}  // namespace

QUERY_TEST(
   DateEqualsTest,
   TEST_DATA,
   ::testing::Values(
      SORTED_DATE_MULTIPLE_MATCHES,
      SORTED_DATE_SINGLE_MATCH,
      UNSORTED_DATE_MULTIPLE_MATCHES,
      UNSORTED_DATE_SINGLE_MATCH,
      SORTED_DATE_NULL_REJECTED,
      UNSORTED_DATE_NULL_REJECTED,
      DATE_EQUALS_NO_MATCH,
      DATE_EQUALS_WRONG_FORMAT,
      DATE_EQUALS_WRONG_VALUE_TYPE,
      DATE_EQUALS_COLUMN_NOT_IN_DB,
      DATE_EQUALS_WRONG_COLUMN_TYPE
   )
);
