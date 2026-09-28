#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const std::optional<bool>& bool_field,
   const std::optional<int32_t>& int_field
) {
   const auto to_json = [](const auto& optional_value) {
      return optional_value.has_value() ? nlohmann::json(optional_value.value())
                                        : nlohmann::json(nullptr);
   };
   nlohmann::json result;
   result["primaryKey"] = primary_key;
   result["boolField"] = to_json(bool_field);
   result["intField"] = to_json(int_field);
   return result;
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "test"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "boolField"
     type: "boolean"
   - name: "intField"
     type: "int"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_true", true, 1),
         createData("id_false", false, 2),
         createData("id_null", std::nullopt, std::nullopt),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario EQUALS_NON_BOOL_COLUMN_REJECTED = {
   .name = "BOOL_EQUALS_NON_BOOL_COLUMN_REJECTED",
   .query = "default.filter(intField = true).project(primaryKey)",
   .expected_error_message = "The column 'intField' is not of type bool",
};

const QueryTestScenario NOT_EQUALS_NON_BOOL_COLUMN_REJECTED = {
   .name = "BOOL_NOT_EQUALS_NON_BOOL_COLUMN_REJECTED",
   .query = "default.filter(intField <> true).project(primaryKey)",
   .expected_error_message = "The column 'intField' is not of type bool",
};

const QueryTestScenario EQUALS_TRUE = {
   .name = "BOOL_EQUALS_TRUE",
   .query = "default.filter(boolField = true).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_true"}])"),
};

const QueryTestScenario EQUALS_FALSE = {
   .name = "BOOL_EQUALS_FALSE",
   .query = "default.filter(boolField = false).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_false"}])"),
};

const QueryTestScenario NOT_EQUALS_TRUE = {
   .name = "BOOL_NOT_EQUALS_TRUE",
   .query = "default.filter(boolField <> true).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_false"}])"),
};

const QueryTestScenario NOT_EQUALS_FALSE = {
   .name = "BOOL_NOT_EQUALS_FALSE",
   .query = "default.filter(boolField <> false).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_true"}])"),
};

const QueryTestScenario EQUALS_COLUMN_ON_RIGHT = {
   .name = "BOOL_EQUALS_COLUMN_ON_RIGHT",
   .query = "default.filter(true = boolField).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_true"}])"),
};

const QueryTestScenario NOT_EQUALS_COLUMN_ON_RIGHT = {
   .name = "BOOL_NOT_EQUALS_COLUMN_ON_RIGHT",
   .query = "default.filter(true <> boolField).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_false"}])"),
};

const QueryTestScenario LESS_THAN_REJECTED = {
   .name = "BOOL_LESS_THAN_REJECTED",
   .query = "default.filter(boolField < true).project(primaryKey)",
   .expected_error_message =
      "The comparison operators <,>,<=,>= are not supported for boolean column 'boolField'",
};

const QueryTestScenario LESS_EQUAL_REJECTED = {
   .name = "BOOL_LESS_EQUAL_REJECTED",
   .query = "default.filter(boolField <= true).project(primaryKey)",
   .expected_error_message =
      "The comparison operators <,>,<=,>= are not supported for boolean column 'boolField'",
};

const QueryTestScenario GREATER_THAN_REJECTED = {
   .name = "BOOL_GREATER_THAN_REJECTED",
   .query = "default.filter(boolField > false).project(primaryKey)",
   .expected_error_message =
      "The comparison operators <,>,<=,>= are not supported for boolean column 'boolField'",
};

const QueryTestScenario GREATER_EQUAL_REJECTED = {
   .name = "BOOL_GREATER_EQUAL_REJECTED",
   .query = "default.filter(boolField >= false).project(primaryKey)",
   .expected_error_message =
      "The comparison operators <,>,<=,>= are not supported for boolean column 'boolField'",
};

// An ordering comparison flips its comparator when the column is on the right, so the
// rejection has to survive the flip as well.
const QueryTestScenario ORDERING_COLUMN_ON_RIGHT_REJECTED = {
   .name = "BOOL_ORDERING_COLUMN_ON_RIGHT_REJECTED",
   .query = "default.filter(true > boolField).project(primaryKey)",
   .expected_error_message =
      "The comparison operators <,>,<=,>= are not supported for boolean column 'boolField'",
};

const QueryTestScenario BARE_BOOL_FILTER = {
   .name = "BOOL_BARE_FILTER",
   .query = "default.filter(boolField).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_true"}])"),
};

const QueryTestScenario NEGATED_BARE_BOOL_FILTER = {
   .name = "BOOL_NEGATED_BARE_FILTER",
   .query = "default.filter(!boolField).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_false"},{"primaryKey":"id_null"}])"),
};

const QueryTestScenario BARE_NON_BOOL_FILTER_REJECTED = {
   .name = "BOOL_BARE_NON_BOOL_FILTER_REJECTED",
   .query = "default.filter(intField).project(primaryKey)",
   .expected_error_message =
      "The column 'intField' is not of type bool and cannot be used directly as a filter predicate",
};

}  // namespace

QUERY_TEST(
   ComparisonBool,
   TEST_DATA,
   ::testing::Values(
      EQUALS_NON_BOOL_COLUMN_REJECTED,
      NOT_EQUALS_NON_BOOL_COLUMN_REJECTED,
      EQUALS_TRUE,
      EQUALS_FALSE,
      NOT_EQUALS_TRUE,
      NOT_EQUALS_FALSE,
      EQUALS_COLUMN_ON_RIGHT,
      NOT_EQUALS_COLUMN_ON_RIGHT,
      LESS_THAN_REJECTED,
      LESS_EQUAL_REJECTED,
      GREATER_THAN_REJECTED,
      GREATER_EQUAL_REJECTED,
      ORDERING_COLUMN_ON_RIGHT_REJECTED,
      BARE_BOOL_FILTER,
      NEGATED_BARE_BOOL_FILTER,
      BARE_NON_BOOL_FILTER_REJECTED
   )
);
