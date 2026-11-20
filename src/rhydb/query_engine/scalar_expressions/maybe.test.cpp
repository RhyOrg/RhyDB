#include "rhydb/query_engine/scalar_expressions/maybe.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

// The tests put `maybe` into a `map`, which is never pushed into the table scan, so the expression
// is evaluated by Arrow (`toArrowExpression`). `Maybe` must evaluate like its child, including
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

const QueryTestScenario MAYBE_COMPARISON_SCENARIO = {
   .name = "MAYBE_COMPARISON_SCENARIO",
   .query = "data.map({r := maybe(x > 0)}).project({primaryKey, r})",
   .expected_query_result = nlohmann::json::parse(R"([
{"primaryKey":"id_0","r":true},
{"primaryKey":"id_1","r":false},
{"primaryKey":"id_2","r":null}
])"),
};

const QueryTestScenario MAYBE_NESTED_SCENARIO = {
   .name = "MAYBE_NESTED_SCENARIO",
   .query = "data.map({r := maybe(x > 0 && x < 5)}).project({primaryKey, r})",
   .expected_query_result = nlohmann::json::parse(R"([
{"primaryKey":"id_0","r":true},
{"primaryKey":"id_1","r":false},
{"primaryKey":"id_2","r":null}
])"),
};

}  // namespace

QUERY_TEST(
   MaybeArrowTest,
   TEST_DATA,
   ::testing::Values(MAYBE_COMPARISON_SCENARIO, MAYBE_NESTED_SCENARIO)
);
