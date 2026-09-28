#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primaryKey,
   const nlohmann::json& int_value,
   const nlohmann::json& date_value
) {
   return {
      {"primaryKey", primaryKey},
      {"int_value", int_value},
      {"segment1", nullptr},
      {"gene1", nullptr},
      {"unaligned_segment1", nullptr},
      {"date", date_value},
   };
}

// Both sort columns contain nulls. `int_value` has a null group (id_0, id_1) so null placement
// is exercised on the *leading* key, and every `int_value` group holds one null and one
// non-null `date` so it is also exercised on the tie-break key. This makes the per-field null
// placement observable and independent: whether a null sorts to the start or the end within a
// group depends only on the direction of that group's own sort key, never on the direction of a
// higher-priority key. RhyDB's contract is that null/missing sorts as the smallest element for
// every field.
const std::vector<nlohmann::json> DATA = {
   createData("id_0", nullptr, nullptr),
   createData("id_1", nullptr, "2023-01-01"),
   createData("id_2", 1, nullptr),
   createData("id_3", 1, "2023-01-01"),
   createData("id_4", 2, nullptr),
   createData("id_5", 2, "2023-01-01"),
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
    - name: "date"
      type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ACGT"}},
   {{"gene1", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// Both keys ascending. Nulls (smallest) sort first everywhere: the null `int_value` group
// leads, and within every group the null `date` comes first.
const QueryTestScenario ASC_THEN_ASC_SCENARIO = {
   .name = "ORDER_BY_ASC_THEN_ASC",
   .query = "default.project({primaryKey, int_value, date}).orderBy({int_value.asc(), date.asc()})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"int_value", nullptr}, {"date", nullptr}},
       {{"primaryKey", "id_1"}, {"int_value", nullptr}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_2"}, {"int_value", 1}, {"date", nullptr}},
       {{"primaryKey", "id_3"}, {"int_value", 1}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_4"}, {"int_value", 2}, {"date", nullptr}},
       {{"primaryKey", "id_5"}, {"int_value", 2}, {"date", "2023-01-01"}},}
   ),
};

// Both keys descending. Nulls (smallest) sort last everywhere: the null `int_value` group
// trails, and within every group the null `date` comes last.
const QueryTestScenario DESC_THEN_DESC_SCENARIO = {
   .name = "ORDER_BY_DESC_THEN_DESC",
   .query =
      "default.project({primaryKey, int_value, date}).orderBy({int_value.desc(), date.desc()})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_5"}, {"int_value", 2}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_4"}, {"int_value", 2}, {"date", nullptr}},
       {{"primaryKey", "id_3"}, {"int_value", 1}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_2"}, {"int_value", 1}, {"date", nullptr}},
       {{"primaryKey", "id_1"}, {"int_value", nullptr}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_0"}, {"int_value", nullptr}, {"date", nullptr}},}
   ),
};

// Leading key descending, tie-break key ascending. Because null sorts as the smallest
// element, the ascending `date` places nulls first within each int_value group - even though
// the leading key is descending - and the null `int_value` group still sorts last. Before
// per-key null placement (arrow < 25.0.0) the single global null placement was derived from
// the first (descending) key, wrongly pushing the `date` nulls to the end of each group.
const QueryTestScenario DESC_THEN_ASC_SCENARIO = {
   .name = "ORDER_BY_DESC_THEN_ASC",
   .query =
      "default.project({primaryKey, int_value, date}).orderBy({int_value.desc(), date.asc()})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_4"}, {"int_value", 2}, {"date", nullptr}},
       {{"primaryKey", "id_5"}, {"int_value", 2}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_2"}, {"int_value", 1}, {"date", nullptr}},
       {{"primaryKey", "id_3"}, {"int_value", 1}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_0"}, {"int_value", nullptr}, {"date", nullptr}},
       {{"primaryKey", "id_1"}, {"int_value", nullptr}, {"date", "2023-01-01"}},}
   ),
};

// Mirror of the above: leading key ascending, tie-break key descending. The descending
// `date` places nulls last within each int_value group, independent of the leading key, while
// the null `int_value` group still sorts first.
const QueryTestScenario ASC_THEN_DESC_SCENARIO = {
   .name = "ORDER_BY_ASC_THEN_DESC",
   .query =
      "default.project({primaryKey, int_value, date}).orderBy({int_value.asc(), date.desc()})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_1"}, {"int_value", nullptr}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_0"}, {"int_value", nullptr}, {"date", nullptr}},
       {{"primaryKey", "id_3"}, {"int_value", 1}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_2"}, {"int_value", 1}, {"date", nullptr}},
       {{"primaryKey", "id_5"}, {"int_value", 2}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_4"}, {"int_value", 2}, {"date", nullptr}},}
   ),
};

// Primary sort key ascending (`date`), with `primaryKey` as deterministic tie-breaker: null
// (smallest) sorts first.
const QueryTestScenario SINGLE_ASC_SCENARIO = {
   .name = "ORDER_BY_SINGLE_ASC",
   .query = "default.project({primaryKey, date}).orderBy({date.asc(), primaryKey.asc()})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"date", nullptr}},
       {{"primaryKey", "id_2"}, {"date", nullptr}},
       {{"primaryKey", "id_4"}, {"date", nullptr}},
       {{"primaryKey", "id_1"}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_3"}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_5"}, {"date", "2023-01-01"}},}
   ),
};

// Single descending key: null (smallest) sorts last.
const QueryTestScenario SINGLE_DESC_SCENARIO = {
   .name = "ORDER_BY_SINGLE_DESC",
   .query = "default.project({primaryKey, date}).orderBy({date.desc(), primaryKey.asc()})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_1"}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_3"}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_5"}, {"date", "2023-01-01"}},
       {{"primaryKey", "id_0"}, {"date", nullptr}},
       {{"primaryKey", "id_2"}, {"date", nullptr}},
       {{"primaryKey", "id_4"}, {"date", nullptr}},}
   ),
};

}  // namespace

QUERY_TEST(
   OrderByNodeTest,
   TEST_DATA,
   ::testing::Values(
      ASC_THEN_ASC_SCENARIO,
      DESC_THEN_DESC_SCENARIO,
      DESC_THEN_ASC_SCENARIO,
      ASC_THEN_DESC_SCENARIO,
      SINGLE_ASC_SCENARIO,
      SINGLE_DESC_SCENARIO
   )
);
