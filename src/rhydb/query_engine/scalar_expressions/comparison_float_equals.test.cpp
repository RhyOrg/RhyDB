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

const QueryTestScenario FLOAT_EQUALS_VALUE_SCENARIO = {
   .name = "FLOAT_EQUALS_VALUE_SCENARIO",
   .query = "default.filter(float_value = 1.23).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"float_value", 1.23}},
       {{"primaryKey", "id_1"}, {"float_value", 1.23}},}
   ),
};

const QueryTestScenario NEGATED_FLOAT_EQUALS_VALUE_SCENARIO = {
   .name = "NEGATED_FLOAT_EQUALS_VALUE_SCENARIO",
   .query = "default.filter(!(float_value = 1.23)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_2"}, {"float_value", 0.345}},
       {{"primaryKey", "id_3"}, {"float_value", 2.345}},
       {{"primaryKey", "id_4"}, {"float_value", nullptr}},}
   ),
};

const QueryTestScenario FLOAT_EQUALS_NULL_REJECTED_SCENARIO = {
   .name = "FLOAT_EQUALS_NULL_REJECTED_SCENARIO",
   .query = "default.filter(float_value = null).project({primaryKey, float_value})",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:30",
};

const QueryTestScenario NEGATED_FLOAT_EQUALS_NULL_REJECTED_SCENARIO = {
   .name = "NEGATED_FLOAT_EQUALS_NULL_REJECTED_SCENARIO",
   .query = "default.filter(!(float_value = null)).project({primaryKey, float_value})",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:32",
};

const QueryTestScenario FLOAT_EQUALS_WITH_INVALID_VALUE = {
   .name = "FLOAT_EQUALS_WITH_INVALID_VALUE",
   .query = "default.filter(float_value = 'something').project({primaryKey, float_value})",
   .expected_error_message = "The column 'float_value' is not of type string",
};

}  // namespace

QUERY_TEST(
   FloatEqualsTest,
   TEST_DATA,
   ::testing::Values(
      FLOAT_EQUALS_VALUE_SCENARIO,
      NEGATED_FLOAT_EQUALS_VALUE_SCENARIO,
      FLOAT_EQUALS_NULL_REJECTED_SCENARIO,
      NEGATED_FLOAT_EQUALS_NULL_REJECTED_SCENARIO,
      FLOAT_EQUALS_WITH_INVALID_VALUE
   )
);
