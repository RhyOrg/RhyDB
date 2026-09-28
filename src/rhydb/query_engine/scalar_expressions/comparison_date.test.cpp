#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const std::string DATE_BELOW = "2020-01-01";
const std::string DATE_BOUND = "2021-06-15";
const std::string DATE_ABOVE = "2023-12-31";

nlohmann::json createData(const std::string& primary_key, const std::optional<std::string>& value) {
   return {
      {"primaryKey", primary_key},
      {"date_value", value.has_value() ? nlohmann::json(value.value()) : nlohmann::json(nullptr)},
   };
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "date_value"
      type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {createData("id_below", DATE_BELOW),
       createData("id_bound", DATE_BOUND),
       createData("id_above", DATE_ABOVE),
       createData("id_null", std::nullopt),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario LESS_THAN = {
   .name = "DATE_LESS_THAN",
   .query = "default.filter(date_value < '2021-06-15'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario LESS_EQUAL = {
   .name = "DATE_LESS_EQUAL",
   .query = "default.filter(date_value <= '2021-06-15'::date).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_bound"}])"),
};

const QueryTestScenario GREATER_THAN = {
   .name = "DATE_GREATER_THAN",
   .query = "default.filter(date_value > '2021-06-15'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_above"}])"),
};

const QueryTestScenario GREATER_EQUAL = {
   .name = "DATE_GREATER_EQUAL",
   .query = "default.filter(date_value >= '2021-06-15'::date).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"}])"),
};

// !(date_value < bound) == date_value >= bound, and nulls are included by the negation.
const QueryTestScenario NEGATED_LESS_THAN = {
   .name = "DATE_NEGATED_LESS_THAN",
   .query = "default.filter(!(date_value < '2021-06-15'::date)).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"},{"primaryKey":"id_null"}])"
   ),
};

// Operand flip: `bound > date_value` must equal `date_value < bound`.
const QueryTestScenario FLIPPED_OPERANDS = {
   .name = "DATE_FLIPPED_OPERANDS",
   .query = "default.filter('2021-06-15'::date > date_value).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario UNKNOWN_COLUMN = {
   .name = "DATE_UNKNOWN_COLUMN",
   .query = "default.filter(does_not_exist < '2021-06-15'::date).project(primaryKey)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'does_not_exist' at 1:16",
};

const QueryTestScenario WRONG_COLUMN_TYPE = {
   .name = "DATE_WRONG_COLUMN_TYPE",
   .query = "default.filter(primaryKey < '2021-06-15'::date).project(primaryKey)",
   .expected_error_message = "The column 'primaryKey' is not of type date",
};

}  // namespace

QUERY_TEST(
   DateComparisonTest,
   TEST_DATA,
   ::testing::Values(
      LESS_THAN,
      LESS_EQUAL,
      GREATER_THAN,
      GREATER_EQUAL,
      NEGATED_LESS_THAN,
      FLIPPED_OPERANDS,
      UNKNOWN_COLUMN,
      WRONG_COLUMN_TYPE
   )
);
