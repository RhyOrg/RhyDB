#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json alignedSequence(const std::string& sequence) {
   return {{"sequence", sequence}, {"insertions", nlohmann::json::array()}};
}

const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"},
    {"str_value", "short"},
    {"segment1", alignedSequence("ACGT")},
    {"gene1", nullptr},},
   {{"primaryKey", "id_1"}, {"str_value", "longlonglong"}, {"segment1", nullptr}, {"gene1", nullptr},
   },
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "at test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "str_value"
      type: "string"
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
   .without_unaligned_sequences = true,
};

const QueryTestScenario AT_STRING_SCENARIO = {
   .name = "AT_STRING",
   .query = "default.map({second := primaryKey.at(4)}).project({primaryKey, second})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"second", "0"}}, {{"primaryKey", "id_1"}, {"second", "1"}}}
   ),
};

// The square-bracket notation `col[i]` is shorthand for `col.at(i)`.
const QueryTestScenario AT_BRACKET_SCENARIO = {
   .name = "AT_BRACKET",
   .query = "default.map({second := primaryKey[4]}).project({primaryKey, second})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"second", "0"}}, {{"primaryKey", "id_1"}, {"second", "1"}}}
   ),
};

const QueryTestScenario AT_STRING_OUT_OF_BOUNDS_SCENARIO = {
   .name = "AT_STRING_OUT_OF_BOUNDS",
   .query = "default.map({eighth := str_value.at(8)}).project({primaryKey, eighth})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"eighth", ""}}, {{"primaryKey", "id_1"}, {"eighth", "g"}}}
   ),
};

const QueryTestScenario AT_SEQUENCE_FIRST_SCENARIO = {
   .name = "AT_SEQUENCE_FIRST",
   .query = "default.map({base := segment1.at(1)}).project({primaryKey, base})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"base", "A"}}, {{"primaryKey", "id_1"}, {"base", nullptr}}}
   ),
};

const QueryTestScenario AT_SEQUENCE_INNER_SCENARIO = {
   .name = "AT_SEQUENCE_INNER",
   .query = "default.map({base := segment1.at(3)}).project({primaryKey, base})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"base", "G"}}, {{"primaryKey", "id_1"}, {"base", nullptr}}}
   ),
};

// A sequence `.at()` predicate cannot be lowered to a bitmap filter (neither side is a plain column
// reference), so applying it directly to the table scan is rejected. Issue #1462 tracks making
// `seq.at(n) = symbol` use an efficient sequence filter here.
const QueryTestScenario AT_FILTER_ON_SCAN_REJECTED_SCENARIO = {
   .name = "AT_FILTER_ON_SCAN_REJECTED",
   .query = "default.filter(segment1.at(1) = 'A').project({primaryKey})",
   .expected_error_message =
      "A Comparison expression can only be compiled to a filter when exactly one side is a column "
      "reference and the other a literal value",
};

// The same `.at()` predicate placed above a limit() cannot be pushed into the scan (limit is a
// pushdown barrier), so it is retained and executed as an Arrow filter over the `.at()` value.
const QueryTestScenario AT_FILTER_ABOVE_LIMIT_SCENARIO = {
   .name = "AT_FILTER_ABOVE_LIMIT",
   .query = "default.limit(2).filter(segment1.at(1) = 'A').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

}  // namespace

QUERY_TEST(
   AtTest,
   TEST_DATA,
   ::testing::Values(
      AT_STRING_SCENARIO,
      AT_BRACKET_SCENARIO,
      AT_STRING_OUT_OF_BOUNDS_SCENARIO,
      AT_SEQUENCE_FIRST_SCENARIO,
      AT_SEQUENCE_INNER_SCENARIO,
      AT_FILTER_ON_SCAN_REJECTED_SCENARIO,
      AT_FILTER_ABOVE_LIMIT_SCENARIO
   )
);
