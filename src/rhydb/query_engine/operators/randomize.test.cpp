#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using nlohmann::json;

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const auto DATA_JSON = R"(
[
  {
    "key": "id1",
    "col": "A",
    "float_value": null,
    "segment1": null,
    "unaligned_segment1": null,
    "gene1": null
  },
  {
    "key": "id2",
    "col": "B",
    "float_value": null,
    "segment1": null,
    "unaligned_segment1": null,
    "gene1": null
  },
  {
    "key": "id3",
    "col": "A",
    "float_value": null,
    "segment1": null,
    "unaligned_segment1": null,
    "gene1": null
  },
  {
    "key": "id4",
    "col": "B",
    "float_value": null,
    "segment1": null,
    "unaligned_segment1": null,
    "gene1": null
  },
  {
    "key": "id5",
    "col": "A",
    "float_value": null,
    "segment1": null,
    "unaligned_segment1": null,
    "gene1": null
  }
]
)";

const std::vector<json> DATA = json::parse(DATA_JSON);

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "key"
      type: "string"
    - name: "col"
      type: "string"
  primaryKey: "key"
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

const QueryTestScenario RANDOMIZE_SEED = {
   .name = "RANDOMIZE_SEED",
   .query = "default.project(key).randomize(seed:=1231)",
   .expected_query_result = json::parse(
      R"([{"key": "id5"},
          {"key": "id1"},
          {"key": "id4"},
          {"key": "id2"},
          {"key": "id3"}])"
   ),
};

const QueryTestScenario RANDOMIZE_INDEPENDENT_ON_COL_NUMS = {
   .name = "RANDOMIZE_INDEPENDENT_ON_COL_NUMS",
   .query = "default.project({key, col}).randomize(seed:=1231)",
   .expected_query_result = json::parse(
      R"(
[{"col":"A","key":"id5"},
{"col":"A","key":"id1"},
{"col":"B","key":"id4"},
{"col":"B","key":"id2"},
{"col":"A","key":"id3"}]
)"
   ),
};

const QueryTestScenario RANDOMIZE_INDEPENDENT_ON_BATCH_SIZE = {
   .name = "RANDOMIZE_INDEPENDENT_ON_BATCH_SIZE",
   .query = "default.project(key).randomize(seed:=1231)",
   .expected_query_result = json::parse(
      R"([{"key": "id5"},
          {"key": "id1"},
          {"key": "id4"},
          {"key": "id2"},
          {"key": "id3"}])"
   ),
   .query_options = rhydb::config::QueryOptions{.materialization_cutoff = 2},
};

const QueryTestScenario DIFFERENT_RANDOMIZE_SEED_DIFFERENT_RESULT = {
   .name = "DIFFERENT_RANDOMIZE_SEED_DIFFERENT_RESULT",
   .query = "default.project(key).randomize(seed:=12312)",
   .expected_query_result = json::parse(
      R"([{"key": "id1"},
          {"key": "id3"},
          {"key": "id5"},
          {"key": "id2"},
          {"key": "id4"}])"
   ),
};

const QueryTestScenario EXPLICIT_DO_NOT_RANDOMIZE = {
   .name = "EXPLICIT_DO_NOT_RANDOMIZE",
   .query = "default.project(key)",
   .expected_query_result = json::parse(
      R"([{"key": "id1"},
          {"key": "id2"},
          {"key": "id3"},
          {"key": "id4"},
          {"key": "id5"}])"
   ),
};

const QueryTestScenario AGGREGATE_RANDOMIZE = {
   .name = "AGGREGATE_RANDOMIZE",
   .query = "default.groupBy({count:=count()},{key}).randomize(seed:=12321)",
   .expected_query_result = json::parse(
      R"([
{"count": 1, "key": "id4"},
{"count": 1, "key": "id5"},
{"count": 1, "key": "id1"},
{"count": 1, "key": "id3"},
{"count": 1, "key": "id2"}
])"
   ),
};

const QueryTestScenario ORDER_BY_PRECEDENCE = {
   .name = "ORDER_BY_PRECEDENCE",
   .query = "default.project({key, col}).randomize(seed:=12321).orderBy({col})",
   .expected_query_result = json::parse(
      R"([
{"key": "id5", "col": "A"},
{"key": "id1", "col": "A"},
{"key": "id3", "col": "A"},
{"key": "id4", "col": "B"},
{"key": "id2", "col": "B"}
])"
   ),
};

const QueryTestScenario ORDER_BY_AGGREGATE_RANDOMIZE = {
   .name = "ORDER_BY_AGGREGATE_RANDOMIZE",
   .query = "default.groupBy({count:=count()},{col}).randomize().orderBy({count})",
   .expected_query_result = json::parse(
      R"([{"count": 2, "col": "B"},
          {"count": 3, "col": "A"}])"
   ),
};

const QueryTestScenario LIMIT_2_RANDOMIZE = {
   .name = "LIMIT_2_RANDOMIZE",
   .query =
      "default.project({key, col}).randomize(seed:=42).offset(2).limit(2).orderBy({col, key})",
   .expected_query_result = json::parse(
      R"([{"key": "id1", "col": "A"},
          {"key": "id5", "col": "A"}])"
   ),
};

const QueryTestScenario LIMIT_3_RANDOMIZE = {
   .name = "LIMIT_3_RANDOMIZE",
   .query =
      "default.project({key, col}).randomize(seed:=42).offset(2).limit(3).orderBy({col, key})",
   .expected_query_result = json::parse(
      R"([{"key": "id1", "col": "A"},
          {"key": "id5", "col": "A"},
          {"key": "id4", "col": "B"}])"
   ),
};

// A randomize directly followed by a limit is combined into an OrderByWithLimitNode that uses the
// top-k (select_k) path. The result must still be the prefix of the full randomized order for this
// seed (see RANDOMIZE_SEED: id5, id1, id4, ...).
const QueryTestScenario RANDOMIZE_WITH_LIMIT = {
   .name = "RANDOMIZE_WITH_LIMIT",
   .query = "default.project(key).randomize(seed:=1231).limit(3)",
   .expected_query_result = json::parse(
      R"([{"key": "id5"},
          {"key": "id1"},
          {"key": "id4"}])"
   ),
};

const QueryTestScenario AGGREGATE_LIMIT_RANDOMIZE = {
   .name = "AGGREGATE_LIMIT_RANDOMIZE",
   .query = "default.groupBy({count:=count()},{key}).randomize(seed:=12321).offset(1).limit(2)",
   .expected_query_result = json::parse(
      R"([{"count": 1, "key": "id5"},
          {"count": 1, "key": "id1"}])"
   ),
};

}  // namespace

QUERY_TEST(
   RandomizeTest,
   TEST_DATA,
   ::testing::Values(
      RANDOMIZE_SEED,
      RANDOMIZE_INDEPENDENT_ON_COL_NUMS,
      RANDOMIZE_INDEPENDENT_ON_BATCH_SIZE,
      DIFFERENT_RANDOMIZE_SEED_DIFFERENT_RESULT,
      EXPLICIT_DO_NOT_RANDOMIZE,
      AGGREGATE_RANDOMIZE,
      ORDER_BY_PRECEDENCE,
      ORDER_BY_AGGREGATE_RANDOMIZE,
      LIMIT_2_RANDOMIZE,
      LIMIT_3_RANDOMIZE,
      RANDOMIZE_WITH_LIMIT,
      AGGREGATE_LIMIT_RANDOMIZE
   )
);
