#include <optional>
#include <string>
#include <vector>

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
   .query = "data.filter(float_value.between(0.5, 1.5)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
   }),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "data.filter(!(float_value.between(0.5, 1.5))).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_2"}, {"float_value", 0.345}},
      {{"primaryKey", "id_3"}, {"float_value", 2.345}},
   }),
};

const QueryTestScenario FLOAT_BETWEEN_INCLUDES_BOTH_BOUNDS_SCENARIO = {
   .name = "FLOAT_BETWEEN_INCLUDES_BOTH_BOUNDS_SCENARIO",
   .query = "data.filter(float_value.between(1.23, 2.345)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
      {{"primaryKey", "id_3"}, {"float_value", 2.345}},
   }),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_EQUAL_BOUNDS_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_EQUAL_BOUNDS_SCENARIO",
   .query = "data.filter(float_value.between(1.23, 1.23)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
   }),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_EQUAL_BOUNDS_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_EQUAL_BOUNDS_SCENARIO",
   .query = "data.filter(!(float_value.between(1.23, 1.23))).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_2"}, {"float_value", 0.345}},
      {{"primaryKey", "id_3"}, {"float_value", 2.345}},
   }),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "data.filter(float_value >= 0.5).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
      {{"primaryKey", "id_3"}, {"float_value", 2.345}},
   }),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "data.filter(!(float_value >= 0.5)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_2"}, {"float_value", 0.345}},
   }),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_TO_SCENARIO",
   .query = "data.filter(float_value < 1.5).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
      {{"primaryKey", "id_2"}, {"float_value", 0.345}},
   }),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_TO_SCENARIO",
   .query = "data.filter(!(float_value < 1.5)).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_3"}, {"float_value", 2.345}},
   }),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "data.filter(float_value.isNotNull()).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"float_value", 1.23}},
      {{"primaryKey", "id_1"}, {"float_value", 1.23}},
      {{"primaryKey", "id_2"}, {"float_value", 0.345}},
      {{"primaryKey", "id_3"}, {"float_value", 2.345}},
   }),
};

const QueryTestScenario NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "data.filter(!(float_value.isNotNull())).project({primaryKey, float_value})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_4"}, {"float_value", nullptr}}}),
};

const QueryTestScenario FLOAT_BETWEEN_WITH_INVALID_FROM_VALUE = {
   .name = "FLOAT_BETWEEN_WITH_INVALID_FROM_VALUE",
   .query = "data.filter(float_value.between('something', 2)).project({primaryKey, float_value})",
   .expected_error_message = "expected integer literal at 1:33",
};

const QueryTestScenario FLOAT_BETWEEN_WITH_INVALID_TO_VALUE = {
   .name = "FLOAT_BETWEEN_WITH_INVALID_TO_VALUE",
   .query = "data.filter(float_value.between(2, 'test')).project({primaryKey, float_value})",
   .expected_error_message = "expected integer literal at 1:36",
};

// The tests below put `between` into a `map`, which is never pushed into the table scan, so the
// expression is evaluated by Arrow (`toArrowExpression`) and not by the bitmap-based `compile`.
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
   const std::string& name,
   const std::string& between_arguments,
   const std::vector<std::optional<bool>>& expected
) {
   return {
      .name = name,
      .query =
         "data.map({b := float_value.between(" + between_arguments + ")}).project({primaryKey, b})",
      .expected_query_result = createBooleanRows(expected),
   };
}

// Values: id_0 = 1.23, id_1 = 1.23, id_2 = 0.345, id_3 = 2.345, id_4 = null
const QueryTestScenario ARROW_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = arrowScenario(
   "ARROW_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   "0.5, 1.5",
   {true, true, false, false, NULL_BOOL}
);
const QueryTestScenario ARROW_FLOAT_BETWEEN_WITH_FROM_SCENARIO = arrowScenario(
   "ARROW_FLOAT_BETWEEN_WITH_FROM_SCENARIO",
   "0.5, null",
   {true, true, false, true, NULL_BOOL}
);
const QueryTestScenario ARROW_FLOAT_BETWEEN_WITH_TO_SCENARIO = arrowScenario(
   "ARROW_FLOAT_BETWEEN_WITH_TO_SCENARIO",
   "null, 1.5",
   {true, true, true, false, NULL_BOOL}
);
const QueryTestScenario ARROW_FLOAT_BETWEEN_BOUNDS_ARE_INCLUSIVE_SCENARIO = arrowScenario(
   "ARROW_FLOAT_BETWEEN_BOUNDS_ARE_INCLUSIVE_SCENARIO",
   "1.23, 2.345",
   {true, true, false, true, NULL_BOOL}
);
const QueryTestScenario ARROW_FLOAT_BETWEEN_WITH_INT_AND_FLOAT_BOUNDS_SCENARIO = arrowScenario(
   "ARROW_FLOAT_BETWEEN_WITH_INT_AND_FLOAT_BOUNDS_SCENARIO",
   "1, 2.5",
   {true, true, false, true, NULL_BOOL}
);

}  // namespace

QUERY_TEST(
   FloatBetweenArrowTest,
   TEST_DATA,
   ::testing::Values(
      ARROW_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      ARROW_FLOAT_BETWEEN_WITH_FROM_SCENARIO,
      ARROW_FLOAT_BETWEEN_WITH_TO_SCENARIO,
      ARROW_FLOAT_BETWEEN_BOUNDS_ARE_INCLUSIVE_SCENARIO,
      ARROW_FLOAT_BETWEEN_WITH_INT_AND_FLOAT_BOUNDS_SCENARIO
   )
);

QUERY_TEST(
   FloatBetweenTest,
   TEST_DATA,
   ::testing::Values(
      FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      NEGATED_FLOAT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      FLOAT_BETWEEN_INCLUDES_BOTH_BOUNDS_SCENARIO,
      FLOAT_BETWEEN_WITH_EQUAL_BOUNDS_SCENARIO,
      NEGATED_FLOAT_BETWEEN_WITH_EQUAL_BOUNDS_SCENARIO,
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
