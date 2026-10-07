# Query Engine

Contains code for public query API.
IMPORTANT: Changes here may require updates to query docs: `documentation/query_documentation.md`.

## Pipeline

```
SaneQL string
  → Parser (saneql/parser.cpp) → AST (saneql/ast.h)
  → ast_to_query.cpp → QueryNode tree (operators/*.h)
  → pipeline optimization passes (optimizer/*.h)
  → addToExecPlan() → Arrow Acero ExecPlan (single shared plan)
  → QueryPlan execution → send result
```

## Key Concepts

- **QueryNode** (`operators/query_node.h`): Base class for all plan nodes.
- **Optimization passes** run sequentially on the QueryNode tree before `addToExecPlan()`.
- **FunctionRegistry**: Maps SaneQL function names to handlers that produce QueryNode trees.

## Arrow Acero Lifecycle

- The planner creates a single `ExecPlan` via `ExecPlan::Make()` and passes it to the root node's `addToExecPlan()`. All nodes add their exec nodes to this shared plan.
- `SinkNodeOptions` takes a pointer to an `AsyncGenerator` that is populated when the plan runs.
- After `StartProducing()`, drain the generator until it returns `nullopt`.
- `StopProducing()` sets a cancelled flag; `plan->finished()` may then resolve with `Cancelled` status — this is normal, not an error.
- Plans must be fully drained or explicitly stopped before destruction.

## QUERY_TEST Pattern

```cpp
#include "rhydb/test/query_fixture.test.h"

namespace {
const auto DATABASE_CONFIG = R"(
schema:
  instanceName: "test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "country"
      type: "string"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = rhydb::ReferenceGenomes{
   {{"segment1", "A"}},
   {{"gene1", "*"}},
};

const rhydb::test::QueryTestData TEST_DATA{
   .ndjson_input_data = {/* JSON objects */},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES
};

const rhydb::test::QueryTestScenario MY_SCENARIO = {
   .name = "MY_TEST_NAME",
   .query = R"(data.filter(country='CH').project({primaryKey}))",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}})
};

// For error cases: set .expected_error_message instead of .expected_query_result
}  // namespace

QUERY_TEST(MyTestSuite, TEST_DATA, ::testing::Values(MY_SCENARIO));
```
