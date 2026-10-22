#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const std::vector<nlohmann::json> DATA = {
   {
      {"primaryKey", "id_0"},
      {"first_name", "Ada"},
      {"last_name", "Lovelace"},
      {"indexed_value", "B.1.1"},
      {"nullable_value", "present"},
      {"segment1", nullptr},
      {"gene1", nullptr},
   },
   {
      {"primaryKey", "id_1"},
      {"first_name", "Alan"},
      {"last_name", "Turing"},
      {"indexed_value", "XBB.1"},
      {"nullable_value", nullptr},
      {"segment1", nullptr},
      {"gene1", nullptr},
   },
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "concat test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "first_name"
      type: "string"
    - name: "last_name"
      type: "string"
    - name: "indexed_value"
      type: "string"
      generateIndex: true
    - name: "nullable_value"
      type: "string"
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
   .without_unaligned_sequences = true,
};

const QueryTestScenario CONCAT_TWO_COLUMNS_SCENARIO = {
   .name = "CONCAT_TWO_COLUMNS",
   .query = "data.map({full := concat(first_name, last_name)}).project({primaryKey, full})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"full", "AdaLovelace"}},
      {{"primaryKey", "id_1"}, {"full", "AlanTuring"}},
   }),
};

const QueryTestScenario CONCAT_METHOD_SYNTAX_SCENARIO = {
   .name = "CONCAT_METHOD_SYNTAX",
   .query = "data.map({full := first_name.concat(last_name)}).project({primaryKey, full})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"full", "AdaLovelace"}},
      {{"primaryKey", "id_1"}, {"full", "AlanTuring"}},
   }),
};

const QueryTestScenario CONCAT_COLUMN_AND_LITERAL_SCENARIO = {
   .name = "CONCAT_COLUMN_AND_LITERAL",
   .query = "data.map({greeting := concat('Hi ', first_name)}).project({primaryKey, greeting})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"greeting", "Hi Ada"}},
      {{"primaryKey", "id_1"}, {"greeting", "Hi Alan"}},
   }),
};

const QueryTestScenario CONCAT_NESTED_SCENARIO = {
   .name = "CONCAT_NESTED",
   .query =
      "data.map({full := concat(concat(first_name, ' '), last_name)}).project({primaryKey, full})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"full", "Ada Lovelace"}},
      {{"primaryKey", "id_1"}, {"full", "Alan Turing"}},
   }),
};

const QueryTestScenario CONCAT_DICTIONARY_ENCODED_SCENARIO = {
   .name = "CONCAT_DICTIONARY_ENCODED",
   .query = "data.map({tagged := concat(indexed_value, '!')}).project({primaryKey, tagged})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"tagged", "B.1.1!"}},
      {{"primaryKey", "id_1"}, {"tagged", "XBB.1!"}},
   }),
};

const QueryTestScenario CONCAT_NULL_INPUT_SCENARIO = {
   .name = "CONCAT_NULL_INPUT",
   .query =
      "data.map({left := concat(nullable_value, '!'), right := '!'.concat(nullable_value)})"
      ".project({primaryKey, left, right})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"left", "present!"}, {"right", "!present"}},
      {{"primaryKey", "id_1"}, {"left", nullptr}, {"right", nullptr}},
   }),
};

// A concat result computed by a map can be filtered above the map.
const QueryTestScenario CONCAT_MAP_THEN_FILTER_SCENARIO = {
   .name = "CONCAT_MAP_THEN_FILTER",
   .query =
      "data.map({full := concat(first_name, last_name)}).filter(full = 'AlanTuring')"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}}}),
};

}  // namespace

QUERY_TEST(
   ConcatTest,
   TEST_DATA,
   ::testing::Values(
      CONCAT_TWO_COLUMNS_SCENARIO,
      CONCAT_METHOD_SYNTAX_SCENARIO,
      CONCAT_COLUMN_AND_LITERAL_SCENARIO,
      CONCAT_NESTED_SCENARIO,
      CONCAT_DICTIONARY_ENCODED_SCENARIO,
      CONCAT_NULL_INPUT_SCENARIO,
      CONCAT_MAP_THEN_FILTER_SCENARIO
   )
);
