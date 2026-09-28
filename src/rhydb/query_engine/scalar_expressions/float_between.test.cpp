#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createDataWithFloatValue(const std::string& primaryKey, double value) {
   return {
      {"primaryKey", primaryKey},
      {"float_value", value},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

nlohmann::json createDataWithFloatNullValue(const std::string& primaryKey) {
   return {
      {"primaryKey", primaryKey},
      {"float_value", nullptr},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

const std::vector<nlohmann::json> DATA = {
   createDataWithFloatValue("id_0", 1.23),
   createDataWithFloatValue("id_1", 1.23),
   createDataWithFloatValue("id_2", 0.345),
   createDataWithFloatValue("id_3", 2.345),
   createDataWithFloatNullValue("id_4"),
};

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

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "A"}},
   {{"gene1", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "default.filter(float_value.between(0.5, 1.5)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
   }),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "default.filter(!(float_value.between(0.5, 1.5))).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_2"}, {"float_value", 0.345}},
       {{"primaryKey", "id_3"}, {"float_value", 2.345}},
       {{"primaryKey", "id_4"}, {"float_value", nullptr}},}
   ),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "default.filter(float_value >= 0.5).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"float_value", 1.23}},
       {{"primaryKey", "id_1"}, {"float_value", 1.23}},
       {{"primaryKey", "id_3"}, {"float_value", 2.345}},}
   ),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "default.filter(!(float_value >= 0.5)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_2"}, {"float_value", 0.345}},
       {{"primaryKey", "id_4"}, {"float_value", nullptr}},}
   ),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_TO_SCENARIO",
   .query = "default.filter(float_value < 1.5).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"float_value", 1.23}},
       {{"primaryKey", "id_1"}, {"float_value", 1.23}},
       {{"primaryKey", "id_2"}, {"float_value", 0.345}},}
   ),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_TO_SCENARIO",
   .query = "default.filter(!(float_value < 1.5)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_3"}, {"float_value", 2.345}},
       {{"primaryKey", "id_4"}, {"float_value", nullptr}},}
   ),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "default.filter(float_value.isNotNull()).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"float_value", 1.23}},
       {{"primaryKey", "id_1"}, {"float_value", 1.23}},
       {{"primaryKey", "id_2"}, {"float_value", 0.345}},
       {{"primaryKey", "id_3"}, {"float_value", 2.345}},}
   ),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "default.filter(!(float_value.isNotNull())).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_4"}, {"float_value", nullptr}}}),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_INVALID_FROM_VALUE = {
   .name = "FLOAT_BETWEEN_WITH_INVALID_FROM_VALUE",
   .query =
      "default.filter(float_value.between('something', 2)).project({primaryKey, float_value})",
   .expected_error_message = "expected integer literal at 1:36",
};

const QueryTestScenario FLOAT_BETWEEN_WITH_INVALID_TO_VALUE = {
   .name = "FLOAT_BETWEEN_WITH_INVALID_TO_VALUE",
   .query = "default.filter(float_value.between(2, 'test')).project({primaryKey, float_value})",
   .expected_error_message = "expected integer literal at 1:39",
};

}  // namespace

QUERY_TEST(
   FloatBetweenTest,
   TEST_DATA,
   ::testing::Values(
      FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      FLOAT_BETWEEN_WITH_FROM_SCENARIO,
      NEGATED_FLOAT_BETWEEN_WITH_FROM_SCENARIO,
      FLOAT_BETWEEN_WITH_TO_SCENARIO,
      NEGATED_FLOAT_BETWEEN_WITH_TO_SCENARIO,
      FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO,
      NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO,
      FLOAT_BETWEEN_WITH_INVALID_FROM_VALUE,
      FLOAT_BETWEEN_WITH_INVALID_TO_VALUE
   )
);
