#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const double BOUND = 3.0;
const double BELOW = 1.5;
const double ABOVE = 5.5;

nlohmann::json createData(const std::string& primary_key, const std::optional<double>& value) {
   return {
      {"primaryKey", primary_key},
      {"float_value", value.has_value() ? nlohmann::json(value.value()) : nlohmann::json(nullptr)},
   };
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "float_value"
      type: "float"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {createData("id_below", BELOW),
       createData("id_bound", BOUND),
       createData("id_above", ABOVE),
       createData("id_null", std::nullopt),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario LESS_THAN = {
   .name = "FLOAT_LESS_THAN",
   .query = "default.filter(float_value < 3.0).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario LESS_EQUAL = {
   .name = "FLOAT_LESS_EQUAL",
   .query = "default.filter(float_value <= 3.0).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_bound"}])"),
};

const QueryTestScenario GREATER_THAN = {
   .name = "FLOAT_GREATER_THAN",
   .query = "default.filter(float_value > 3.0).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_above"}])"),
};

const QueryTestScenario GREATER_EQUAL = {
   .name = "FLOAT_GREATER_EQUAL",
   .query = "default.filter(float_value >= 3.0).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"}])"),
};

// !(float_value < 3.0) == float_value >= 3.0, and nulls are included by the negation.
const QueryTestScenario NEGATED_LESS_THAN = {
   .name = "FLOAT_NEGATED_LESS_THAN",
   .query = "default.filter(!(float_value < 3.0)).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"},{"primaryKey":"id_null"}])"
   ),
};

// Operand flip: `3.0 > float_value` must equal `float_value < 3.0`.
const QueryTestScenario FLIPPED_OPERANDS = {
   .name = "FLOAT_FLIPPED_OPERANDS",
   .query = "default.filter(3.0 > float_value).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario TYPE_MISMATCH = {
   .name = "FLOAT_TYPE_MISMATCH",
   .query = "default.filter(float_value < 'x').project(primaryKey)",
   .expected_error_message = "The column 'float_value' is not of type string",
};

const QueryTestScenario UNKNOWN_COLUMN = {
   .name = "FLOAT_UNKNOWN_COLUMN",
   .query = "default.filter(does_not_exist < 3.0).project(primaryKey)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'does_not_exist' at 1:16",
};

}  // namespace

QUERY_TEST(
   FloatComparisonTest,
   TEST_DATA,
   ::testing::Values(
      LESS_THAN,
      LESS_EQUAL,
      GREATER_THAN,
      GREATER_EQUAL,
      NEGATED_LESS_THAN,
      FLIPPED_OPERANDS,
      TYPE_MISMATCH,
      UNKNOWN_COLUMN
   )
);
