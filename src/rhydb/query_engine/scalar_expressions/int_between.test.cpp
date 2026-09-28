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

const QueryTestScenario INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "default.filter(int_value.between(2, 4))",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"},
       {"int_value", VALUE_IN_FILTER},
       {"segment1", nullptr},
       {"gene1", nullptr},
       {"unaligned_segment1", nullptr},},
      {{"primaryKey", "id_1"},
       {"int_value", VALUE_IN_FILTER},
       {"segment1", nullptr},
       {"gene1", nullptr},
       {"unaligned_segment1", nullptr},},
   }),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "default.filter(!(int_value.between(2, 4)))",
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

const QueryTestScenario INT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "INT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "default.filter(int_value >= 2)",
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
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_3"},
        {"int_value", VALUE_ABOVE_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},}
   ),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "default.filter(!(int_value >= 2))",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_2"},
        {"int_value", VALUE_BELOW_FILTER},
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

const QueryTestScenario INT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "INT_BETWEEN_WITH_TO_SCENARIO",
   .query = "default.filter(int_value <= 4)",
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
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_2"},
        {"int_value", VALUE_BELOW_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},}
   ),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_TO_SCENARIO",
   .query = "default.filter(!(int_value <= 4))",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_3"},
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

const QueryTestScenario INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "default.filter(int_value.isNotNull())",
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
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_2"},
        {"int_value", VALUE_BELOW_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},
       {{"primaryKey", "id_3"},
        {"int_value", VALUE_ABOVE_FILTER},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},}
   ),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "default.filter(!(int_value.isNotNull()))",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_4"},
        {"int_value", nullptr},
        {"segment1", nullptr},
        {"gene1", nullptr},
        {"unaligned_segment1", nullptr},},}
   ),
};

const QueryTestScenario INT_COMPARE_WITH_OVERFLOW = {
   .name = "INT_COMPARE_WITH_OVERFLOW",
   .query = "default.filter(int_value >= 4294967295)",
   .expected_error_message = "Cannot cast 4294967295 to int32. Value out of range",
};

}  // namespace

QUERY_TEST(
   IntBetweenTest,
   TEST_DATA,
   ::testing::Values(
      INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      INT_BETWEEN_WITH_FROM_SCENARIO,
      NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO,
      INT_BETWEEN_WITH_TO_SCENARIO,
      NEGATED_INT_BETWEEN_WITH_TO_SCENARIO,
      INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO,
      NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO,
      INT_COMPARE_WITH_OVERFLOW
   )
);
