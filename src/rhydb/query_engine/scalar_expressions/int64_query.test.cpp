#include <cstdint>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

// All values are deliberately outside the 32-bit range to prove int64 storage and query routing.
const int64_t VALUE_IN_FILTER = 5'000'000'000LL;
const int64_t VALUE_BELOW_FILTER = 3'000'000'000LL;
const int64_t VALUE_ABOVE_FILTER = 10'000'000'000LL;

nlohmann::json createDataWithInt64Value(const std::string& primaryKey, int64_t value) {
   return {
      {"primaryKey", primaryKey},
      {"int64_value", value},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   };
}

nlohmann::json createDataWithInt64NullValue(const std::string& primaryKey) {
   return {
      {"primaryKey", primaryKey},
      {"int64_value", nullptr},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   };
}

const std::vector DATA = {
   createDataWithInt64Value("id_0", VALUE_IN_FILTER),
   createDataWithInt64Value("id_1", VALUE_IN_FILTER),
   createDataWithInt64Value("id_2", VALUE_BELOW_FILTER),
   createDataWithInt64Value("id_3", VALUE_ABOVE_FILTER),
   createDataWithInt64NullValue("id_4"),
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "int64_value"
      type: "int64"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "A"}},
   {{"gene1", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

nlohmann::json row(const std::string& primaryKey, nlohmann::json value) {
   return {
      {"primaryKey", primaryKey},
      {"int64_value", std::move(value)},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   };
}

const QueryTestScenario INT64_EQUALS_VALUE_SCENARIO = {
   .name = "INT64_EQUALS_VALUE_SCENARIO",
   .query = "default.filter(int64_value = 5000000000)",
   .expected_query_result =
      nlohmann::json({row("id_0", VALUE_IN_FILTER), row("id_1", VALUE_IN_FILTER)}),
};

const QueryTestScenario INT64_GREATER_EQUAL_SCENARIO = {
   .name = "INT64_GREATER_EQUAL_SCENARIO",
   .query = "default.filter(int64_value >= 5000000000)",
   .expected_query_result = nlohmann::json(
      {row("id_0", VALUE_IN_FILTER), row("id_1", VALUE_IN_FILTER), row("id_3", VALUE_ABOVE_FILTER)}
   ),
};

const QueryTestScenario INT64_BETWEEN_SCENARIO = {
   .name = "INT64_BETWEEN_SCENARIO",
   .query = "default.filter(between(int64_value, 4000000000, 9000000000))",
   .expected_query_result =
      nlohmann::json({row("id_0", VALUE_IN_FILTER), row("id_1", VALUE_IN_FILTER)}),
};

const QueryTestScenario INT64_EQUALS_NULL_REJECTED_SCENARIO = {
   .name = "INT64_EQUALS_NULL_REJECTED_SCENARIO",
   .query = "default.filter(int64_value = null)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a "
      "column reference, or a scalar function call at 1:30",
};

const QueryTestScenario INT64_NEGATED_EQUALS_SCENARIO = {
   .name = "INT64_NEGATED_EQUALS_SCENARIO",
   .query = "default.filter(!(int64_value = 5000000000))",
   .expected_query_result = nlohmann::json(
      {row("id_2", VALUE_BELOW_FILTER), row("id_3", VALUE_ABOVE_FILTER), row("id_4", nullptr)}
   ),
};

const QueryTestScenario INT64_EQUALS_INT32_RANGE_VALUE_SCENARIO = {
   .name = "INT64_EQUALS_INT32_RANGE_VALUE_SCENARIO",
   .query = "default.filter(int64_value = 100)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario INT64_EQUALS_FUNCTION_CALL_VALUE_SCENARIO = {
   .name = "INT64_EQUALS_FUNCTION_CALL_VALUE_SCENARIO",
   .query = "default.filter(int64_value = primaryKey.at(1))",
   .expected_error_message =
      "Unsupported value type in comparison with column 'int64_value': the value must be an int, "
      "float, date, string, or bool literal",
};

}  // namespace

QUERY_TEST(
   Int64QueryTest,
   TEST_DATA,
   ::testing::Values(
      INT64_EQUALS_VALUE_SCENARIO,
      INT64_GREATER_EQUAL_SCENARIO,
      INT64_BETWEEN_SCENARIO,
      INT64_EQUALS_NULL_REJECTED_SCENARIO,
      INT64_NEGATED_EQUALS_SCENARIO,
      INT64_EQUALS_INT32_RANGE_VALUE_SCENARIO,
      INT64_EQUALS_FUNCTION_CALL_VALUE_SCENARIO
   )
);
