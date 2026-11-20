#include "rhydb/query_engine/scalar_expressions/exact.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

// The tests put `exact` into a `map`, which is never pushed into the table scan, so the expression
// is evaluated by Arrow (`toArrowExpression`). `Exact` must evaluate like its child, including
// null.
const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "x"
      type: "int"
  primaryKey: "primaryKey"
)";

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         {{"primaryKey", "id_0"}, {"x", 1}},
         {{"primaryKey", "id_1"}, {"x", -1}},
         {{"primaryKey", "id_2"}, {"x", nullptr}},
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = ReferenceGenomes{{}, {}},
};

const QueryTestScenario EXACT_COMPARISON_SCENARIO = {
   .name = "EXACT_COMPARISON_SCENARIO",
   .query = "data.map({r := exact(x > 0)}).project({primaryKey, r})",
   .expected_query_result = nlohmann::json::parse(R"([
{"primaryKey":"id_0","r":true},
{"primaryKey":"id_1","r":false},
{"primaryKey":"id_2","r":null}
])"),
};

const QueryTestScenario EXACT_NESTED_SCENARIO = {
   .name = "EXACT_NESTED_SCENARIO",
   .query = "data.map({r := exact(x > 0 && x < 5)}).project({primaryKey, r})",
   .expected_query_result = nlohmann::json::parse(R"([
{"primaryKey":"id_0","r":true},
{"primaryKey":"id_1","r":false},
{"primaryKey":"id_2","r":null}
])"),
};

}  // namespace

QUERY_TEST(
   ExactArrowTest,
   TEST_DATA,
   ::testing::Values(EXACT_COMPARISON_SCENARIO, EXACT_NESTED_SCENARIO)
);
