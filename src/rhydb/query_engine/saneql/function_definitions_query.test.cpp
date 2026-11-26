#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

// Queries with `let` definitions, executed against a database.

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"}, {"country", "Switzerland"}, {"age", 5}},
   {{"primaryKey", "id_1"}, {"country", "Germany"}, {"age", 17}},
   {{"primaryKey", "id_2"}, {"country", "Germany"}, {"age", 30}},
   {{"primaryKey", "id_3"}, {"country", "France"}, {"age", 42}},
   {{"primaryKey", "id_4"}, {"country", "Germany"}, {"age", 64}},
   {{"primaryKey", "id_5"}, {"country", "France"}, {"age", 12}},
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

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = ReferenceGenomes{{}, {}},
};

const QueryTestScenario VIEW = {
   .name = "VIEW",
   .query =
      "let adults := data.filter(age >= 18).project({primaryKey, country}),\n"
      "adults.filter(country = 'Germany')",
   .expected_query_result = nlohmann::json::parse(R"(
[{"primaryKey":"id_2","country":"Germany"},
{"primaryKey":"id_4","country":"Germany"}]
)"),
};

// The example of query_documentation.md: `c` is a column of the argument.
const QueryTestScenario SYMBOLS = {
   .name = "SYMBOLS",
   .query =
      "let frequent(t table, c symbol, minimum scalar := 1) :=\n"
      "   t.group(by := {c}, aggs := {n := count()}).filter(n > minimum).projectout({n}),\n"
      "data.frequent(country).order(by := {country})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"country":"France"},
{"country":"Germany"}]
)"),
};

const QueryTestScenario SCALAR_ARGUMENT = {
   .name = "SCALAR_ARGUMENT",
   .query =
      "let frequent(t table, c symbol, minimum scalar := 1) :=\n"
      "   t.group(by := {c}, aggs := {n := count()}).filter(n > minimum).projectout({n}),\n"
      "data.frequent(country, minimum := 2)",
   .expected_query_result = nlohmann::json::parse(R"(
[{"country":"Germany"}]
)"),
};

// An expression argument is resolved in the body, here against a column that the body creates.
const QueryTestScenario EXPRESSION_IS_RESOLVED_IN_THE_BODY = {
   .name = "EXPRESSION_IS_RESOLVED_IN_THE_BODY",
   .query =
      "let withYears(t table, p expression) :=\n"
      "   t.map({years := age}).filter(p).project({primaryKey, years}),\n"
      "data.withYears(years > 40)",
   .expected_query_result = nlohmann::json::parse(R"(
[{"primaryKey":"id_3","years":42},
{"primaryKey":"id_4","years":64}]
)"),
};

const QueryTestScenario SCALAR_ARGUMENT_MUST_BE_CONSTANT = {
   .name = "SCALAR_ARGUMENT_MUST_BE_CONSTANT",
   .query =
      "let older(t table, limit scalar := 18) := t.filter(age > limit),\n"
      "data.older(age)",
   .expected_error_message =
      "older(): the argument 'limit' must be a constant, as it is a scalar, but age at 2:12 "
      "refers to a column or table; declare the parameter as 'limit expression' to pass an "
      "expression",
};

}  // namespace

QUERY_TEST(
   FunctionDefinitionsQueryTest,
   TEST_DATA,
   ::testing::Values(
      VIEW,
      SYMBOLS,
      SCALAR_ARGUMENT,
      EXPRESSION_IS_RESOLVED_IN_THE_BODY,
      SCALAR_ARGUMENT_MUST_BE_CONSTANT
   )
)
