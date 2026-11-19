#include <optional>
#include <string>
#include <vector>

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
   {
      {"primaryKey", "id"},
      {"sorted_date", SORTED_DATE_VALUE},
      {"unsorted_date", UNSORTED_DATE_VALUE},
   },
};

const QueryTestScenario SORTED_DATE_WITH_TO_AND_FROM_SCENARIO = {
   .name = "SORTED_DATE_WITH_TO_AND_FROM_SCENARIO",
   .query = "data.filter(sorted_date.between('2020-12-24'::date, '2020-12-24'::date))",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario SORTED_DATE_WITH_TO_ONLY_SCENARIO = {
   .name = "SORTED_DATE_WITH_TO_ONLY_SCENARIO",
   .query = "data.filter(sorted_date <= '2020-12-24'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario SORTED_DATE_WITH_FROM_ONLY_SCENARIO = {
   .name = "SORTED_DATE_WITH_FROM_ONLY_SCENARIO",
   .query = "data.filter(sorted_date >= '2020-12-24'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_TO_AND_FROM_SCENARIO = {
   .name = "UNSORTED_DATE_WITH_TO_AND_FROM_SCENARIO",
   .query = "data.filter(unsorted_date.between('2023-01-20'::date, '2023-01-20'::date))",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_TO_ONLY_SCENARIO = {
   .name = "UNSORTED_DATE_WITH_TO_ONLY_SCENARIO",
   .query = "data.filter(unsorted_date <= '2023-01-20'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_FROM_ONLY_SCENARIO = {
   .name = "UNSORTED_DATE_WITH_FROM_ONLY_SCENARIO",
   .query = "data.filter(unsorted_date >= '2023-01-20'::date)",
   .expected_query_result = EXPECTED_RESULT,
};

const QueryTestScenario UNSORTED_DATE_WITH_COLUMN_NOT_IN_DB = {
   .name = "UNSORTED_DATE_WITH_COLUMN_NOT_IN_DB",
   .query = "data.filter(something_not_in_database >= '2000-01-01'::date)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'something_not_in_database' at 1:13",
};

const QueryTestScenario UNSORTED_DATE_WITH_NON_DATE_COLUMN = {
   .name = "UNSORTED_DATE_WITH_NON_DATE_COLUMN",
   .query = "data.filter(primaryKey >= '2020-01-01'::date)",
   .expected_error_message = "The column 'primaryKey' is not of type date",
};

// The tests below put `between` into a `map`, which is never pushed into the table scan, so the
// expression is evaluated by Arrow (`toArrowExpression`) and not by the bitmap-based `compile`.
nlohmann::json createArrowData(
   const std::string& primary_key,
   const std::optional<std::string>& date
) {
   const auto json_date = date.has_value() ? nlohmann::json(date.value()) : nlohmann::json(nullptr);
   return {
      {"primaryKey", primary_key},
      {"plain_date", json_date},
      {"indexed_date", json_date},
   };
}

const auto ARROW_DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "plain_date"
      type: "date"
    - name: "indexed_date"
      type: "date"
      generateIndex: true
  primaryKey: "primaryKey"
)";

const QueryTestData ARROW_TEST_DATA{
   .ndjson_input_data =
      {
         createArrowData("id_0", "2023-01-20"),
         createArrowData("id_1", "2023-01-20"),
         createArrowData("id_2", "2022-01-01"),
         createArrowData("id_3", "2024-06-01"),
         createArrowData("id_4", std::nullopt),
      },
   .database_config = ARROW_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

nlohmann::json createBooleanRows(const std::vector<std::optional<bool>>& values) {
   auto rows = nlohmann::json::array();
   for (size_t i = 0; i < values.size(); ++i) {
      rows.push_back({
         {"primaryKey", "id_" + std::to_string(i)},
         {"b", values[i].has_value() ? nlohmann::json(values[i].value()) : nlohmann::json(nullptr)},
      });
   }
   return rows;
}

const std::optional<bool> NULL_BOOL = std::nullopt;

QueryTestScenario arrowScenario(
   const std::string& column,
   const std::string& name,
   const std::string& between_arguments,
   const std::vector<std::optional<bool>>& expected
) {
   return {
      .name = column + "_" + name,
      .query = "data.map({b := " + column + ".between(" + between_arguments +
               ")}).project({primaryKey, b})",
      .expected_query_result = createBooleanRows(expected),
   };
}

const std::string FROM = "'2022-06-01'::date";
const std::string TO = "'2023-12-31'::date"; // NOLINT(readability-identifier-length)

const QueryTestScenario ARROW_PLAIN_DATE_WITH_FROM_AND_TO_SCENARIO = arrowScenario(
   "plain_date",
   "ARROW_WITH_FROM_AND_TO_SCENARIO",
   FROM + ", " + TO,
   {true, true, false, false, NULL_BOOL}
);
const QueryTestScenario ARROW_PLAIN_DATE_WITH_FROM_SCENARIO = arrowScenario(
   "plain_date",
   "ARROW_WITH_FROM_SCENARIO",
   FROM + ", null",
   {true, true, false, true, NULL_BOOL}
);
const QueryTestScenario ARROW_PLAIN_DATE_WITH_TO_SCENARIO = arrowScenario(
   "plain_date",
   "ARROW_WITH_TO_SCENARIO",
   "null, " + TO,
   {true, true, true, false, NULL_BOOL}
);
const QueryTestScenario ARROW_PLAIN_DATE_BOUNDS_ARE_INCLUSIVE_SCENARIO = arrowScenario(
   "plain_date",
   "ARROW_BOUNDS_ARE_INCLUSIVE_SCENARIO",
   "'2023-01-20'::date, '2023-01-20'::date",
   {true, true, false, false, NULL_BOOL}
);

const QueryTestScenario ARROW_INDEXED_DATE_WITH_FROM_AND_TO_SCENARIO = arrowScenario(
   "indexed_date",
   "ARROW_WITH_FROM_AND_TO_SCENARIO",
   FROM + ", " + TO,
   {true, true, false, false, NULL_BOOL}
);
const QueryTestScenario ARROW_INDEXED_DATE_WITH_FROM_SCENARIO = arrowScenario(
   "indexed_date",
   "ARROW_WITH_FROM_SCENARIO",
   FROM + ", null",
   {true, true, false, true, NULL_BOOL}
);
const QueryTestScenario ARROW_INDEXED_DATE_WITH_TO_SCENARIO = arrowScenario(
   "indexed_date",
   "ARROW_WITH_TO_SCENARIO",
   "null, " + TO,
   {true, true, true, false, NULL_BOOL}
);

}  // namespace

QUERY_TEST(
   DateBetweenArrowTest,
   ARROW_TEST_DATA,
   ::testing::Values(
      ARROW_PLAIN_DATE_WITH_FROM_AND_TO_SCENARIO,
      ARROW_PLAIN_DATE_WITH_FROM_SCENARIO,
      ARROW_PLAIN_DATE_WITH_TO_SCENARIO,
      ARROW_PLAIN_DATE_BOUNDS_ARE_INCLUSIVE_SCENARIO,
      ARROW_INDEXED_DATE_WITH_FROM_AND_TO_SCENARIO,
      ARROW_INDEXED_DATE_WITH_FROM_SCENARIO,
      ARROW_INDEXED_DATE_WITH_TO_SCENARIO
   )
);

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
