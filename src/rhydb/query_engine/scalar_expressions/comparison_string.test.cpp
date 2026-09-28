#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

// A value longer than 12 bytes is stored with a variable-length suffix, so it
// exercises the StringColumn fast/slow compare path in CompareToValueSelection.
const std::string LONG_VALUE = "watermelonwatermelon";

nlohmann::json createData(const std::string& primary_key, const std::optional<std::string>& value) {
   return {
      {"primaryKey", primary_key},
      {"stringField", value.has_value() ? nlohmann::json(value.value()) : nlohmann::json(nullptr)},
      {"dictField", value.has_value() ? nlohmann::json(value.value()) : nlohmann::json(nullptr)},
   };
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "stringField"
      type: "string"
    - name: "dictField"
      type: "string"
      generateIndex: true
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {createData("id_apple", "apple"),
       createData("id_banana", "banana"),
       createData("id_cherry", "cherry"),
       createData("id_null", std::nullopt),
       createData("id_long", LONG_VALUE),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// --- plain string column ---

const QueryTestScenario STRING_LESS_THAN = {
   .name = "STRING_LESS_THAN",
   .query = "default.filter(stringField < 'banana').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_apple"}])"),
};

const QueryTestScenario STRING_LESS_EQUAL = {
   .name = "STRING_LESS_EQUAL",
   .query = "default.filter(stringField <= 'banana').project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_apple"},{"primaryKey":"id_banana"}])"),
};

const QueryTestScenario STRING_GREATER_THAN = {
   .name = "STRING_GREATER_THAN",
   .query = "default.filter(stringField > 'banana').project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_cherry"},{"primaryKey":"id_long"}])"),
};

const QueryTestScenario STRING_GREATER_EQUAL = {
   .name = "STRING_GREATER_EQUAL",
   .query = "default.filter(stringField >= 'banana').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_banana"},{"primaryKey":"id_cherry"},{"primaryKey":"id_long"}])"
   ),
};

// !(stringField < 'banana') keeps everything at/above the bound plus the null row.
const QueryTestScenario STRING_NEGATED_LESS_THAN = {
   .name = "STRING_NEGATED_LESS_THAN",
   .query = "default.filter(!(stringField < 'banana')).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_banana"},{"primaryKey":"id_cherry"},{"primaryKey":"id_null"},{"primaryKey":"id_long"}])"
   ),
};

// Operand flip: `'banana' > stringField` must equal `stringField < 'banana'`.
const QueryTestScenario STRING_FLIPPED_OPERANDS = {
   .name = "STRING_FLIPPED_OPERANDS",
   .query = "default.filter('banana' > stringField).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_apple"}])"),
};

// Long literal sharing a >4-byte prefix with the long row value forces the
// lexicographic slow-path fall-back of the German-string comparison.
const QueryTestScenario STRING_LONG_VALUE = {
   .name = "STRING_LONG_VALUE",
   .query = "default.filter(stringField > 'watermelonwatermel').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_long"}])"),
};

// --- dictionary-encoded string column (bitmap-union fast path) ---

const QueryTestScenario DICT_LESS_THAN = {
   .name = "DICT_LESS_THAN",
   .query = "default.filter(dictField < 'banana').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_apple"}])"),
};

const QueryTestScenario DICT_LESS_EQUAL = {
   .name = "DICT_LESS_EQUAL",
   .query = "default.filter(dictField <= 'banana').project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_apple"},{"primaryKey":"id_banana"}])"),
};

const QueryTestScenario DICT_GREATER_EQUAL = {
   .name = "DICT_GREATER_EQUAL",
   .query = "default.filter(dictField >= 'banana').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_banana"},{"primaryKey":"id_cherry"},{"primaryKey":"id_long"}])"
   ),
};

// Nulls are excluded from the dictionary index, so a negation still drops the null row's
// dictionary bitmap but the negated selection re-adds it via null handling.
const QueryTestScenario DICT_NEGATED_LESS_THAN = {
   .name = "DICT_NEGATED_LESS_THAN",
   .query = "default.filter(!(dictField < 'banana')).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_banana"},{"primaryKey":"id_cherry"},{"primaryKey":"id_null"},{"primaryKey":"id_long"}])"
   ),
};

const QueryTestScenario DICT_FLIPPED_OPERANDS = {
   .name = "DICT_FLIPPED_OPERANDS",
   .query = "default.filter('banana' > dictField).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_apple"}])"),
};

const QueryTestScenario STRING_UNKNOWN_COLUMN = {
   .name = "STRING_UNKNOWN_COLUMN",
   .query = "default.filter(does_not_exist < 'banana').project(primaryKey)",
   .expected_error_message =
      "the left side of a comparison references unknown column 'does_not_exist' at 1:16",
};

}  // namespace

QUERY_TEST(
   StringComparisonTest,
   TEST_DATA,
   ::testing::Values(
      STRING_LESS_THAN,
      STRING_LESS_EQUAL,
      STRING_GREATER_THAN,
      STRING_GREATER_EQUAL,
      STRING_NEGATED_LESS_THAN,
      STRING_FLIPPED_OPERANDS,
      STRING_LONG_VALUE,
      DICT_LESS_THAN,
      DICT_LESS_EQUAL,
      DICT_GREATER_EQUAL,
      DICT_NEGATED_LESS_THAN,
      DICT_FLIPPED_OPERANDS,
      STRING_UNKNOWN_COLUMN
   )
);
