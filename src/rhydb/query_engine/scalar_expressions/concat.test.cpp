#include <string>
#include <vector>

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
   {
      {"primaryKey", "id_0"},
      {"first_name", "Ada"},
      {"last_name", "Lovelace"},
      {"indexed_value", "B.1.1"},
      {"nullable_value", "present"},
      {"birth_year", 1815},
      {"birth_date", "1815-12-10"},
      {"segment1", alignedSequence("ACGT")},
      {"unaligned_segment1", "ACGTA"},
      {"gene1", nullptr},
   },
   {
      {"primaryKey", "id_1"},
      {"first_name", "Alan"},
      {"last_name", "Turing"},
      {"indexed_value", "XBB.1"},
      {"nullable_value", nullptr},
      {"birth_year", 1912},
      {"birth_date", "1912-06-23"},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
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
    - name: "birth_year"
      type: "int"
    - name: "birth_date"
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

const QueryTestScenario CONCAT_MAP_THEN_FILTER_SCENARIO = {
   .name = "CONCAT_MAP_THEN_FILTER",
   .query =
      "data.map({full := concat(first_name, last_name)}).filter(full = 'AlanTuring')"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}}}),
};

const QueryTestScenario CONCAT_SEQUENCE_CHARACTER_SCENARIO = {
   .name = "CONCAT_SEQUENCE_CHARACTER",
   .query =
      "data.map({base := concat(segment1.at(1), segment1.at(2))}).project({primaryKey, base})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"base", "AC"}},
      {{"primaryKey", "id_1"}, {"base", nullptr}},
   }),
};

const QueryTestScenario CONCAT_SEQUENCE_COLUMN_SCENARIO = {
   .name = "CONCAT_SEQUENCE_COLUMN",
   .query = "data.map({tagged := concat(segment1, '!')}).project({primaryKey, tagged})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"tagged", "ACGT!"}},
      {{"primaryKey", "id_1"}, {"tagged", nullptr}},
   }),
};

const QueryTestScenario CONCAT_UNALIGNED_SEQUENCE_COLUMN_SCENARIO = {
   .name = "CONCAT_UNALIGNED_SEQUENCE_COLUMN",
   .query = "data.map({tagged := concat(unaligned_segment1, '!')}).project({primaryKey, tagged})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"tagged", "ACGTA!"}},
      {{"primaryKey", "id_1"}, {"tagged", nullptr}},
   }),
};

const QueryTestScenario CONCAT_INT_COLUMN_THROWS_SCENARIO = {
   .name = "CONCAT_INT_COLUMN_THROWS",
   .query = "data.map({x := concat(first_name, birth_year)}).project({primaryKey, x})",
   .expected_error_message =
      "concat(): the right argument birth_year must be a string, but is of type INT32",
};

const QueryTestScenario CONCAT_DATE_COLUMN_THROWS_SCENARIO = {
   .name = "CONCAT_DATE_COLUMN_THROWS",
   .query = "data.map({x := concat(birth_date, first_name)}).project({primaryKey, x})",
   .expected_error_message =
      "concat(): the left argument birth_date must be a string, but is of type DATE32",
};

const QueryTestScenario CONCAT_INT_LITERAL_THROWS_SCENARIO = {
   .name = "CONCAT_INT_LITERAL_THROWS",
   .query = "data.map({x := concat(first_name, 42)}).project({primaryKey, x})",
   .expected_error_message =
      "concat(): the right argument 42 must be a string, but is of type INT64",
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
      CONCAT_MAP_THEN_FILTER_SCENARIO,
      CONCAT_SEQUENCE_CHARACTER_SCENARIO,
      CONCAT_SEQUENCE_COLUMN_SCENARIO,
      CONCAT_UNALIGNED_SEQUENCE_COLUMN_SCENARIO,
      CONCAT_INT_COLUMN_THROWS_SCENARIO,
      CONCAT_DATE_COLUMN_THROWS_SCENARIO,
      CONCAT_INT_LITERAL_THROWS_SCENARIO
   )
);
