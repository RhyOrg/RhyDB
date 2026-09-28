#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const int VALUE_IN_FILTER = 3;
const int VALUE_BELOW_FILTER = 1;
const int VALUE_ABOVE_FILTER = 5;

nlohmann::json createDataWithIntValue(const std::string& primaryKey, int value) {
   return {
      {"primaryKey", primaryKey},
      {"int_value", value},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   };
}

nlohmann::json createDataWithIntNullValue(const std::string& primaryKey) {
   return {
      {"primaryKey", primaryKey},
      {"int_value", nullptr},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   };
}

const std::vector<nlohmann::json> DATA = {
   createDataWithIntValue("id_0", VALUE_IN_FILTER),
   createDataWithIntValue("id_1", VALUE_IN_FILTER),
   createDataWithIntValue("id_2", VALUE_BELOW_FILTER),
   createDataWithIntValue("id_3", VALUE_ABOVE_FILTER),
   createDataWithIntNullValue("id_4"),
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "int_value"
      type: "int"
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

const QueryTestScenario INT_EQUALS_VALUE_SCENARIO = {
   .name = "INT_EQUALS_VALUE_SCENARIO",
   .query = "default.filter(int_value = 3)",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"},
        {"int_value", VALUE_IN_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_1"},
        {"int_value", VALUE_IN_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},}
   ),
};

const QueryTestScenario NEGATED_INT_EQUALS_VALUE_SCENARIO = {
   .name = "NEGATED_INT_EQUALS_VALUE_SCENARIO",
   .query = "default.filter(!(int_value = 3))",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_2"},
        {"int_value", VALUE_BELOW_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_3"},
        {"int_value", VALUE_ABOVE_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_4"},
        {"int_value", nullptr},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},}
   ),
};

const QueryTestScenario INT_EQUALS_NULL_REJECTED_SCENARIO = {
   .name = "INT_EQUALS_NULL_REJECTED_SCENARIO",
   .query = "default.filter(int_value = null)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:28",
};

const QueryTestScenario NEGATED_INT_EQUALS_NULL_REJECTED_SCENARIO = {
   .name = "NEGATED_INT_EQUALS_NULL_REJECTED_SCENARIO",
   .query = "default.filter(!(int_value = null))",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:30",
};

const QueryTestScenario INT_NOT_EQUALS_NULL_REJECTED_SCENARIO = {
   .name = "INT_NOT_EQUALS_NULL_REJECTED_SCENARIO",
   .query = "default.filter(int_value <> null)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:29",
};

const QueryTestScenario INT_EQUALS_WITH_OVERFLOW = {
   .name = "INT_EQUALS_WITH_OVERFLOW",
   .query = "default.filter(int_value = 4294967295)",
   .expected_error_message = "Cannot cast 4294967295 to int32. Value out of range",
};

const QueryTestScenario INT_COMPARISON_WITH_OVERFLOW = {
   .name = "INT_COMPARISON_WITH_OVERFLOW",
   .query = "default.filter(int_value >= 4294967295)",
   .expected_error_message = "Cannot cast 4294967295 to int32. Value out of range",
};

const QueryTestScenario INT_BETWEEN_WITH_OVERFLOW = {
   .name = "INT_BETWEEN_WITH_OVERFLOW",
   .query = "default.filter(between(int_value, 0, 4294967295))",
   .expected_error_message = "Cannot cast 4294967295 to int32. Value out of range",
};

}  // namespace

QUERY_TEST(
   IntEqualsTest,
   TEST_DATA,
   ::testing::Values(
      INT_EQUALS_VALUE_SCENARIO,
      NEGATED_INT_EQUALS_VALUE_SCENARIO,
      INT_EQUALS_NULL_REJECTED_SCENARIO,
      NEGATED_INT_EQUALS_NULL_REJECTED_SCENARIO,
      INT_NOT_EQUALS_NULL_REJECTED_SCENARIO,
      INT_EQUALS_WITH_OVERFLOW,
      INT_COMPARISON_WITH_OVERFLOW,
      INT_BETWEEN_WITH_OVERFLOW
   )
);
