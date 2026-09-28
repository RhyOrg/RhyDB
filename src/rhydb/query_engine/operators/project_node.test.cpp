#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"}, {"country", "Switzerland"}, {"age", 5}},
   {{"primaryKey", "id_1"}, {"country", "Germany"}, {"age", 7}},
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "country"
      type: "string"
    - name: "age"
      type: "int"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario PROJECT_EMPTY_SCENARIO = {
   .name = "PROJECT_EMPTY",
   .query = "default.project({})",
   .expected_error_message = "a projection must keep at least one column",
};

const QueryTestScenario PROJECTOUT_SET_SCENARIO = {
   .name = "PROJECTOUT_SET",
   .query = "default.projectout({age})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"country", "Switzerland"}},
       {{"primaryKey", "id_1"}, {"country", "Germany"}},}
   ),
};

const QueryTestScenario PROJECTOUT_SINGLE_SCENARIO = {
   .name = "PROJECTOUT_SINGLE",
   .query = "default.projectout(country)",
   .expected_query_result =
      nlohmann::json({{{"primaryKey", "id_0"}, {"age", 5}}, {{"primaryKey", "id_1"}, {"age", 7}}}),
};

const QueryTestScenario PROJECTOUT_MULTIPLE_SCENARIO = {
   .name = "PROJECTOUT_MULTIPLE",
   .query = "default.projectout({country, age})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}, {{"primaryKey", "id_1"}}}),
};

const QueryTestScenario PROJECTOUT_OVER_GROUP_BY_SCENARIO = {
   .name = "PROJECTOUT_OVER_GROUP_BY",
   .query = "default.groupBy({count := count()}, {country}).orderBy({country}).projectout({count})",
   .expected_query_result =
      nlohmann::json({{{"country", "Germany"}}, {{"country", "Switzerland"}}}),
};

const QueryTestScenario PROJECTOUT_ALL_SCENARIO = {
   .name = "PROJECTOUT_ALL",
   .query = "default.projectout({primaryKey, country, age})",
   .expected_error_message = "a projection must keep at least one column",
};

const QueryTestScenario PROJECTOUT_UNKNOWN_COLUMN_SCENARIO = {
   .name = "PROJECTOUT_UNKNOWN_COLUMN",
   .query = "default.projectout({doesNotExist})",
   .expected_error_message =
      "projectout field 'doesNotExist' is not present in the input's output schema",
};

}  // namespace

QUERY_TEST(
   ProjectoutTest,
   TEST_DATA,
   ::testing::Values(
      PROJECT_EMPTY_SCENARIO,
      PROJECTOUT_SET_SCENARIO,
      PROJECTOUT_SINGLE_SCENARIO,
      PROJECTOUT_MULTIPLE_SCENARIO,
      PROJECTOUT_OVER_GROUP_BY_SCENARIO,
      PROJECTOUT_ALL_SCENARIO,
      PROJECTOUT_UNKNOWN_COLUMN_SCENARIO
   )
);
