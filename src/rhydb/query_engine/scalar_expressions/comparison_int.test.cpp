#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const int BOUND = 3;
const int BELOW = 1;
const int ABOVE = 5;

// int64_value mirrors int_value but scaled outside the 32-bit range
const int64_t INT64_SCALE = 1'000'000'000LL;

nlohmann::json createData(const std::string& primary_key, const std::optional<int>& value) {
   return {
      {"primaryKey", primary_key},
      {"int_value", value.has_value() ? nlohmann::json(value.value()) : nlohmann::json(nullptr)},
      {
         "int64_value",
         value.has_value() ? nlohmann::json(value.value() * INT64_SCALE) : nlohmann::json(nullptr),
      },
      {"bool_value", nullptr},
   };
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "int_value"
      type: "int"
    - name: "int64_value"
      type: "int64"
    - name: "bool_value"
      type: "boolean"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_below", BELOW),
         createData("id_bound", BOUND),
         createData("id_above", ABOVE),
         createData("id_null", std::nullopt),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario LESS_THAN = {
   .name = "INT_LESS_THAN",
   .query = "data.filter(int_value < 3).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario LESS_EQUAL = {
   .name = "INT_LESS_EQUAL",
   .query = "data.filter(int_value <= 3).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_bound"}])"),
};

const QueryTestScenario GREATER_THAN = {
   .name = "INT_GREATER_THAN",
   .query = "data.filter(int_value > 3).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_above"}])"),
};

const QueryTestScenario GREATER_EQUAL = {
   .name = "INT_GREATER_EQUAL",
   .query = "data.filter(int_value >= 3).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"}])"),
};

// !(int_value < 3) == int_value >= 3: the comparison is null on a null value, and so is its
// negation.
const QueryTestScenario NEGATED_LESS_THAN = {
   .name = "INT_NEGATED_LESS_THAN",
   .query = "data.filter(!(int_value < 3)).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"}])"),
};

// !(int_value > 3) == int_value <= 3
const QueryTestScenario NEGATED_GREATER_THAN = {
   .name = "INT_NEGATED_GREATER_THAN",
   .query = "data.filter(!(int_value > 3)).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_bound"}])"),
};

// !(int_value >= 3) == int_value < 3
const QueryTestScenario NEGATED_GREATER_EQUAL = {
   .name = "INT_NEGATED_GREATER_EQUAL",
   .query = "data.filter(!(int_value >= 3)).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

// A conjunction is null on the null row, so is its negation
const QueryTestScenario NEGATED_CONJUNCTION = {
   .name = "INT_NEGATED_CONJUNCTION",
   .query = "data.filter(!(int_value > 1 && int_value < 5)).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_above"}])"),
};

// A disjunction is null on the null row, so is its negation
const QueryTestScenario NEGATED_DISJUNCTION = {
   .name = "INT_NEGATED_DISJUNCTION",
   .query = "data.filter(!(int_value < 2 || int_value > 4)).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_bound"}])"),
};

// An n-of is null on a row if its result depends on its null children
const QueryTestScenario NEGATED_N_OF = {
   .name = "INT_NEGATED_N_OF",
   .query = "data.filter(!(nOf(1, {int_value < 3, int_value > 3}))).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_bound"}])"),
};

const QueryTestScenario NEGATED_EXACT_N_OF = {
   .name = "INT_NEGATED_EXACT_N_OF",
   .query =
      "data.filter(!(nOf(1, {int_value < 3, int_value <= 3}, matchExactly:=true)))"
      ".project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_above"}])"),
};

// bool_value is null on every row, so exactly one child might be true on every row, but on none
// it is certain
const QueryTestScenario EXACT_N_OF_WITH_NULL_CHILD = {
   .name = "INT_EXACT_N_OF_WITH_NULL_CHILD",
   .query =
      "data.filter(nOf(1, {int_value < 3, bool_value = true}, matchExactly:=true))"
      ".project(primaryKey)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario DOUBLE_NEGATION = {
   .name = "INT_DOUBLE_NEGATION",
   .query = "data.filter(!(!(int_value < 3))).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

// Operand flip: `3 > int_value` must equal `int_value < 3`.
const QueryTestScenario FLIPPED_OPERANDS = {
   .name = "INT_FLIPPED_OPERANDS",
   .query = "data.filter(3 > int_value).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario FLIPPED_OPERANDS_INCLUSIVE = {
   .name = "INT_FLIPPED_OPERANDS_INCLUSIVE",
   .query = "data.filter(3 >= int_value).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_below"},{"primaryKey":"id_bound"}])"),
};

const QueryTestScenario TYPE_MISMATCH = {
   .name = "INT_TYPE_MISMATCH",
   .query = "data.filter(int_value < 'x').project(primaryKey)",
   .expected_error_message = "The column 'int_value' is not of type string",
};

const QueryTestScenario BOOL_COMPARISON = {
   .name = "INT_BOOL_COMPARISON",
   .query = "data.filter(bool_value < true).project(primaryKey)",
   .expected_error_message =
      "The comparison operators <,>,<=,>= are not supported for boolean column 'bool_value'",
};

const QueryTestScenario UNKNOWN_COLUMN = {
   .name = "INT_UNKNOWN_COLUMN",
   .query = "data.filter(does_not_exist < 3).project(primaryKey)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'does_not_exist' at 1:13",
};

const QueryTestScenario TWO_COLUMNS = {
   .name = "INT_TWO_COLUMNS",
   .query = "data.filter(int_value < primaryKey).project(primaryKey)",
   .expected_error_message =
      "A Comparison expression can only be compiled to a filter when exactly one side is a column "
      "reference and the other a literal value",
};

const QueryTestScenario NO_COLUMN = {
   .name = "INT_NO_COLUMN",
   .query = "data.filter(1 < 2).project(primaryKey)",
   .expected_error_message =
      "A Comparison expression can only be compiled to a filter when exactly one side is a column "
      "reference and the other a literal value",
};

const QueryTestScenario NO_LITERAL = {
   .name = "INT_NO_LITERAL",
   .query = "data.filter(int_value < primaryKey.at(1)).project(primaryKey)",
   .expected_error_message =
      "Unsupported value type in comparison with column 'int_value': the value must be an int, "
      "float, date, string, or bool literal",
};

const QueryTestScenario INT32_OVERFLOW = {
   .name = "INT_INT32_OVERFLOW",
   .query = "data.filter(int_value < 3000000000).project(primaryKey)",
   .expected_error_message = "Cannot cast 3000000000 to int32. Value out of range",
};

const QueryTestScenario INT64_LESS_THAN = {
   .name = "INT_INT64_LESS_THAN",
   .query = "data.filter(int64_value < 3000000000).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

const QueryTestScenario INT64_GREATER_EQUAL = {
   .name = "INT_INT64_GREATER_EQUAL",
   .query = "data.filter(int64_value >= 3000000000).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_bound"},{"primaryKey":"id_above"}])"),
};

const QueryTestScenario INT64_FLIPPED_OPERANDS = {
   .name = "INT_INT64_FLIPPED_OPERANDS",
   .query = "data.filter(3000000000 > int64_value).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_below"}])"),
};

}  // namespace

QUERY_TEST(
   IntComparisonTest,
   TEST_DATA,
   ::testing::Values(
      LESS_THAN,
      LESS_EQUAL,
      GREATER_THAN,
      GREATER_EQUAL,
      NEGATED_LESS_THAN,
      NEGATED_GREATER_THAN,
      NEGATED_GREATER_EQUAL,
      NEGATED_CONJUNCTION,
      NEGATED_DISJUNCTION,
      DOUBLE_NEGATION,
      NEGATED_N_OF,
      NEGATED_EXACT_N_OF,
      EXACT_N_OF_WITH_NULL_CHILD,
      FLIPPED_OPERANDS,
      FLIPPED_OPERANDS_INCLUSIVE,
      TYPE_MISMATCH,
      BOOL_COMPARISON,
      UNKNOWN_COLUMN,
      TWO_COLUMNS,
      NO_COLUMN,
      NO_LITERAL,
      INT32_OVERFLOW,
      INT64_LESS_THAN,
      INT64_GREATER_EQUAL,
      INT64_FLIPPED_OPERANDS
   )
);
