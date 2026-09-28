#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const std::optional<std::string>& string_field,
   const std::optional<std::string>& dictionary_encoded_string_field
) {
   nlohmann::json result;
   result["primaryKey"] = primary_key;
   result["stringField"] =
      string_field.has_value() ? nlohmann::json(string_field.value()) : nlohmann::json(nullptr);
   result["dictionaryEncodedStringField"] =
      dictionary_encoded_string_field.has_value()
         ? nlohmann::json(dictionary_encoded_string_field.value())
         : nlohmann::json(nullptr);
   return result;
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "test"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "stringField"
     type: "string"
   - name: "dictionaryEncodedStringField"
     type: "string"
     generateIndex: true
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_0", "value1", "indexed1"),
         createData("id_1", std::nullopt, "indexed2"),
         createData("id_2", "value2", std::nullopt),
         createData("id_3", "value3", "indexed3"),
         createData("id_4", std::nullopt, std::nullopt),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario STRING_EQUALS_NULL_REJECTED_STRING_COLUMN = {
   .name = "STRING_EQUALS_NULL_REJECTED_STRING_COLUMN",
   .query = "default.filter(stringField = null).project(primaryKey)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:30",
};

const QueryTestScenario STRING_EQUALS_NULL_REJECTED_DICTIONARY_ENCODED_COLUMN = {
   .name = "STRING_EQUALS_NULL_REJECTED_DICTIONARY_ENCODED_COLUMN",
   .query = "default.filter(dictionaryEncodedStringField = null).project(primaryKey)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:47",
};

const QueryTestScenario STRING_EQUALS_NULL_REJECTED_NEGATED = {
   .name = "STRING_EQUALS_NULL_REJECTED_NEGATED",
   .query = "default.filter(!(stringField = null)).project(primaryKey)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:32",
};

const QueryTestScenario STRING_EQUALS_VALUE = {
   .name = "STRING_EQUALS_VALUE",
   .query = "default.filter(stringField = 'value1').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_0"}])"),
};

const QueryTestScenario STRING_EQUALS_INDEXED_VALUE = {
   .name = "STRING_EQUALS_INDEXED_VALUE",
   .query = "default.filter(dictionaryEncodedStringField = 'indexed1').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_0"}])"),
};

const QueryTestScenario STRING_EQUALS_NO_MATCH = {
   .name = "STRING_EQUALS_NO_MATCH",
   .query = "default.filter(stringField = 'nonexistent').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

}  // namespace

QUERY_TEST(
   StringEquals,
   TEST_DATA,
   ::testing::Values(
      STRING_EQUALS_NULL_REJECTED_STRING_COLUMN,
      STRING_EQUALS_NULL_REJECTED_DICTIONARY_ENCODED_COLUMN,
      STRING_EQUALS_NULL_REJECTED_NEGATED,
      STRING_EQUALS_VALUE,
      STRING_EQUALS_INDEXED_VALUE,
      STRING_EQUALS_NO_MATCH
   )
);
