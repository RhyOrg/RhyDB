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
   .query = "data.filter(int_value.between(2, 4))",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_0"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_1"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "data.filter(!(int_value.between(2, 4)))",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_2"},
         {"int_value", VALUE_BELOW_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_3"},
         {"int_value", VALUE_ABOVE_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario INT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "INT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "data.filter(int_value >= 2)",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_0"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_1"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_3"},
         {"int_value", VALUE_ABOVE_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "data.filter(!(int_value >= 2))",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_2"},
         {"int_value", VALUE_BELOW_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario INT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "INT_BETWEEN_WITH_TO_SCENARIO",
   .query = "data.filter(int_value <= 4)",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_0"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_1"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_2"},
         {"int_value", VALUE_BELOW_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_TO_SCENARIO",
   .query = "data.filter(!(int_value <= 4))",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_3"},
         {"int_value", VALUE_ABOVE_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "data.filter(int_value.isNotNull())",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_0"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_1"},
         {"int_value", VALUE_IN_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_2"},
         {"int_value", VALUE_BELOW_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
      {
         {"primaryKey", "id_3"},
         {"int_value", VALUE_ABOVE_FILTER},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

const QueryTestScenario NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO = {
   .name = "NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_NULL_SCENARIO",
   .query = "data.filter(!(int_value.isNotNull()))",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_4"},
         {"int_value", nullptr},
         {"segment1", nullptr},
         {"gene1", nullptr},
         {"unaligned_segment1", nullptr},
      },
   }),
};

// The filters on a column created by `map` are not pushed down into the table scan, so they are
// evaluated as Arrow expressions.
const QueryTestScenario ARROW_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "ARROW_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "data.map({x := int_value}).filter(x.between(2, 4)).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}},
      {{"primaryKey", "id_1"}},
   }),
};

const QueryTestScenario ARROW_NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "ARROW_NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query = "data.map({x := int_value}).filter(!(x.between(2, 4))).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_2"}},
      {{"primaryKey", "id_3"}},
   }),
};

const QueryTestScenario ARROW_INT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "ARROW_INT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "data.map({x := int_value}).filter(x.between(2, null)).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}},
      {{"primaryKey", "id_1"}},
      {{"primaryKey", "id_3"}},
   }),
};

const QueryTestScenario ARROW_NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "ARROW_NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO",
   .query = "data.map({x := int_value}).filter(!(x.between(2, null))).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_2"}},
   }),
};

const QueryTestScenario ARROW_INT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "ARROW_INT_BETWEEN_WITH_TO_SCENARIO",
   .query = "data.map({x := int_value}).filter(x.between(null, 4)).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}},
      {{"primaryKey", "id_1"}},
      {{"primaryKey", "id_2"}},
   }),
};

const QueryTestScenario ARROW_NEGATED_INT_BETWEEN_WITH_TO_SCENARIO = {
   .name = "ARROW_NEGATED_INT_BETWEEN_WITH_TO_SCENARIO",
   .query = "data.map({x := int_value}).filter(!(x.between(null, 4))).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_3"}},
   }),
};

const QueryTestScenario ARROW_INT_BETWEEN_WITH_BOUND_BEYOND_INT32_SCENARIO = {
   .name = "ARROW_INT_BETWEEN_WITH_BOUND_BEYOND_INT32_SCENARIO",
   .query = "data.map({x := int_value}).filter(x.between(4294967295, null)).project({primaryKey})",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario INT_COMPARE_WITH_OVERFLOW = {
   .name = "INT_COMPARE_WITH_OVERFLOW",
   .query = "data.filter(int_value >= 4294967295)",
   .expected_error_message = "Cannot cast 4294967295 to int32. Value out of range",
};

const int64_t INT64_VALUE_BELOW_FILTER = 1;
const int64_t INT64_VALUE_IN_FILTER = 4'000'000'000;
const int64_t INT64_VALUE_ABOVE_FILTER = 6'000'000'000;

nlohmann::json createDataWithInt64Value(const std::string& primaryKey, int64_t value) {
   return {
      {"primaryKey", primaryKey},
      {"int64_value", value},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   };
}

const std::vector<nlohmann::json> INT64_DATA = {
   createDataWithInt64Value("id_0", INT64_VALUE_IN_FILTER),
   createDataWithInt64Value("id_1", INT64_VALUE_BELOW_FILTER),
   createDataWithInt64Value("id_2", INT64_VALUE_ABOVE_FILTER),
   {
      {"primaryKey", "id_3"},
      {"int64_value", nullptr},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
   },
};

const auto INT64_DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "int64_value"
      type: "int64"
  primaryKey: "primaryKey"
)";

const QueryTestData INT64_TEST_DATA{
   .ndjson_input_data = INT64_DATA,
   .database_config = INT64_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario ARROW_INT64_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "ARROW_INT64_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query =
      "data.map({x := int64_value}).filter(x.between(2000000000, 5000000000))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}},
   }),
};

const QueryTestScenario ARROW_NEGATED_INT64_BETWEEN_WITH_FROM_AND_TO_SCENARIO = {
   .name = "ARROW_NEGATED_INT64_BETWEEN_WITH_FROM_AND_TO_SCENARIO",
   .query =
      "data.map({x := int64_value}).filter(!(x.between(2000000000, 5000000000)))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_1"}},
      {{"primaryKey", "id_2"}},
   }),
};

const QueryTestScenario ARROW_INT64_BETWEEN_WITH_FROM_SCENARIO = {
   .name = "ARROW_INT64_BETWEEN_WITH_FROM_SCENARIO",
   .query =
      "data.map({x := int64_value}).filter(x.between(5000000000, null)).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_2"}},
   }),
};

const QueryTestScenario ARROW_INT64_BETWEEN_WITH_TO_SCENARIO = {
   .name = "ARROW_INT64_BETWEEN_WITH_TO_SCENARIO",
   .query =
      "data.map({x := int64_value}).filter(x.between(null, 5000000000)).project({primaryKey})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}},
      {{"primaryKey", "id_1"}},
   }),
};

}  // namespace

QUERY_TEST(
   Int64BetweenTest,
   INT64_TEST_DATA,
   ::testing::Values(
      ARROW_INT64_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      ARROW_NEGATED_INT64_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      ARROW_INT64_BETWEEN_WITH_FROM_SCENARIO,
      ARROW_INT64_BETWEEN_WITH_TO_SCENARIO
   )
);

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
      ARROW_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      ARROW_NEGATED_INT_BETWEEN_WITH_FROM_AND_TO_SCENARIO,
      ARROW_INT_BETWEEN_WITH_FROM_SCENARIO,
      ARROW_NEGATED_INT_BETWEEN_WITH_FROM_SCENARIO,
      ARROW_INT_BETWEEN_WITH_TO_SCENARIO,
      ARROW_NEGATED_INT_BETWEEN_WITH_TO_SCENARIO,
      ARROW_INT_BETWEEN_WITH_BOUND_BEYOND_INT32_SCENARIO,
      INT_COMPARE_WITH_OVERFLOW
   )
);
