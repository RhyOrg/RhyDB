#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const std::optional<std::string>& string_field,
   const std::optional<std::string>& dict_field,
   const std::optional<int32_t>& int_field,
   const std::optional<int64_t>& int64_field,
   const std::optional<double>& float_field,
   const std::optional<std::string>& date_field,
   const std::optional<bool>& bool_field
) {
   const auto to_json = [](const auto& optional_value) {
      return optional_value.has_value() ? nlohmann::json(optional_value.value())
                                        : nlohmann::json(nullptr);
   };
   nlohmann::json result;
   result["primaryKey"] = primary_key;
   result["stringField"] = to_json(string_field);
   result["dictField"] = to_json(dict_field);
   result["intField"] = to_json(int_field);
   result["int64Field"] = to_json(int64_field);
   result["floatField"] = to_json(float_field);
   result["dateField"] = to_json(date_field);
   result["boolField"] = to_json(bool_field);
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
   - name: "dictField"
     type: "string"
     generateIndex: true
   - name: "intField"
     type: "int"
   - name: "int64Field"
     type: "int64"
   - name: "floatField"
     type: "float"
   - name: "dateField"
     type: "date"
   - name: "boolField"
     type: "boolean"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

// Outside the int32 range
const int64_t INT64_VALUE = 5'000'000'001LL;
const int64_t OTHER_INT64_VALUE = 5'000'000'002LL;

// id_2 is null in every column, so it is the row that distinguishes "<> excludes
// nulls" from the old negated-equality behaviour.
const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_0", "value1", "indexed1", 1, INT64_VALUE, 1.5, "2021-01-01", true),
         createData("id_1", "value2", "indexed2", 2, OTHER_INT64_VALUE, 2.5, "2021-02-01", false),
         createData(
            "id_2",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt
         ),
         createData("id_3", "value1", "indexed1", 1, INT64_VALUE, 1.5, "2021-01-01", true),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario NOT_EQUALS_STRING_PLAIN = {
   .name = "NOT_EQUALS_STRING_PLAIN",
   .query = "default.filter(stringField <> 'value1').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_STRING_DICT = {
   .name = "NOT_EQUALS_STRING_DICT",
   .query = "default.filter(dictField <> 'indexed1').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_INT = {
   .name = "NOT_EQUALS_INT",
   .query = "default.filter(intField <> 1).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_INT64 = {
   .name = "NOT_EQUALS_INT64",
   .query = "default.filter(int64Field <> 5000000001).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_FLOAT = {
   .name = "NOT_EQUALS_FLOAT",
   .query = "default.filter(floatField <> 1.5).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_DATE = {
   .name = "NOT_EQUALS_DATE",
   .query = "default.filter(dateField <> '2021-01-01'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_BOOL_TRUE = {
   .name = "NOT_EQUALS_BOOL_TRUE",
   .query = "default.filter(boolField <> true).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_BOOL_FALSE = {
   .name = "NOT_EQUALS_BOOL_FALSE",
   .query = "default.filter(boolField <> false).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"}])"),
};

const QueryTestScenario NOT_EQUALS_VALUE_NOT_PRESENT = {
   .name = "NOT_EQUALS_VALUE_NOT_PRESENT",
   .query = "default.filter(intField <> 999).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_1"},{"primaryKey":"id_3"}])"
      ),
};

const QueryTestScenario NOT_EQUALS_DICT_VALUE_NOT_IN_DICTIONARY = {
   .name = "NOT_EQUALS_DICT_VALUE_NOT_IN_DICTIONARY",
   .query = "default.filter(dictField <> 'never_indexed').project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_1"},{"primaryKey":"id_3"}])"
      ),
};

const QueryTestScenario NOT_EQUALS_DICT_EXCLUDES_MATCHING_VALUE = {
   .name = "NOT_EQUALS_DICT_EXCLUDES_MATCHING_VALUE",
   .query = "default.filter(dictField <> 'indexed2').project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"}])"),
};

const QueryTestScenario NOT_EQUALS_COLUMN_ON_RIGHT = {
   .name = "NOT_EQUALS_COLUMN_ON_RIGHT",
   .query = "default.filter('value1' <> stringField).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario NOT_EQUALS_DICT_COLUMN_ON_RIGHT = {
   .name = "NOT_EQUALS_DICT_COLUMN_ON_RIGHT",
   .query = "default.filter('indexed2' <> dictField).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"}])"),
};

const QueryTestScenario NOT_EQUALS_DICT_IN_CONJUNCTION = {
   .name = "NOT_EQUALS_DICT_IN_CONJUNCTION",
   .query = "default.filter(dictField <> 'indexed2' && intField <> 999).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"}])"),
};

const QueryTestScenario NOT_EQUALS_NULL_PLAIN = {
   .name = "NOT_EQUALS_NULL_PLAIN",
   .query = "default.filter(stringField <> null).project(primaryKey)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:31",
};

const QueryTestScenario NOT_EQUALS_NULL_DICT = {
   .name = "NOT_EQUALS_NULL_DICT",
   .query = "default.filter(dictField <> null).project(primaryKey)",
   .expected_error_message =
      "the right side of a comparison must be a literal value (int, float, string, bool, or date), "
      "a column reference, or a scalar function call at 1:29",
};

// --- `!` is a set complement, not SQL's NOT, so it keeps the null rows that `<>`
// drops. The two are deliberately different. ---

const QueryTestScenario NEGATED_EQUALS_STILL_INCLUDES_NULLS = {
   .name = "NEGATED_EQUALS_STILL_INCLUDES_NULLS",
   .query = "default.filter(!(stringField = 'value1')).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_1"},{"primaryKey":"id_2"}])"),
};

}  // namespace

QUERY_TEST(
   ComparisonNotEquals,
   TEST_DATA,
   ::testing::Values(
      NOT_EQUALS_STRING_PLAIN,
      NOT_EQUALS_STRING_DICT,
      NOT_EQUALS_INT,
      NOT_EQUALS_INT64,
      NOT_EQUALS_FLOAT,
      NOT_EQUALS_DATE,
      NOT_EQUALS_BOOL_TRUE,
      NOT_EQUALS_BOOL_FALSE,
      NOT_EQUALS_VALUE_NOT_PRESENT,
      NOT_EQUALS_DICT_VALUE_NOT_IN_DICTIONARY,
      NOT_EQUALS_DICT_EXCLUDES_MATCHING_VALUE,
      NOT_EQUALS_COLUMN_ON_RIGHT,
      NOT_EQUALS_DICT_COLUMN_ON_RIGHT,
      NOT_EQUALS_DICT_IN_CONJUNCTION,
      NOT_EQUALS_NULL_PLAIN,
      NOT_EQUALS_NULL_DICT,
      NEGATED_EQUALS_STILL_INCLUDES_NULLS
   )
);

// --- Empty-result edge for the dictionary complement path: when every non-null row
// holds the excluded value, the complement covers no rows and must collapse to Empty
// rather than a Complement that evaluates to nothing. ---

namespace {
const auto SINGLE_VALUE_DATABASE_CONFIG =
   R"(
schema:
  instanceName: "test"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "dictField"
     type: "string"
     generateIndex: true
  primaryKey: "primaryKey"
)";

nlohmann::json createSingleValueData(
   const std::string& primary_key,
   const std::optional<std::string>& dict_field
) {
   nlohmann::json result;
   result["primaryKey"] = primary_key;
   result["dictField"] =
      dict_field.has_value() ? nlohmann::json(dict_field.value()) : nlohmann::json(nullptr);
   return result;
}

// Every non-null row holds "only", so `dictField <> 'only'` excludes all rows.
const QueryTestData SINGLE_VALUE_TEST_DATA{
   .ndjson_input_data =
      {
         createSingleValueData("id_a", "only"),
         createSingleValueData("id_b", "only"),
         createSingleValueData("id_c", std::nullopt),
      },
   .database_config = SINGLE_VALUE_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario NOT_EQUALS_DICT_MATCHES_NO_ROWS = {
   .name = "NOT_EQUALS_DICT_MATCHES_NO_ROWS",
   .query = "default.filter(dictField <> 'only').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};
}  // namespace

QUERY_TEST(
   ComparisonNotEqualsEmpty,
   SINGLE_VALUE_TEST_DATA,
   ::testing::Values(NOT_EQUALS_DICT_MATCHES_NO_ROWS)
);

// --- A dictionary column without any null row: only the literal has to be excluded, so
// there is no second bitmap to intersect with. ---

namespace {
const QueryTestData NO_NULLS_TEST_DATA{
   .ndjson_input_data =
      {
         createSingleValueData("id_a", "a"),
         createSingleValueData("id_b", "b"),
      },
   .database_config = SINGLE_VALUE_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario NOT_EQUALS_DICT_NO_NULLS = {
   .name = "NOT_EQUALS_DICT_NO_NULLS",
   .query = "default.filter(dictField <> 'a').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_b"}])"),
};

const QueryTestScenario NOT_EQUALS_DICT_NO_NULLS_LITERAL_ABSENT = {
   .name = "NOT_EQUALS_DICT_NO_NULLS_LITERAL_ABSENT",
   .query = "default.filter(dictField <> 'never_indexed').project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_a"},{"primaryKey":"id_b"}])"),
};
}  // namespace

QUERY_TEST(
   ComparisonNotEqualsNoNulls,
   NO_NULLS_TEST_DATA,
   ::testing::Values(NOT_EQUALS_DICT_NO_NULLS, NOT_EQUALS_DICT_NO_NULLS_LITERAL_ABSENT)
);
