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
      {"int_col", 42},
      {"long_col", 5000000000},
      {"float_col", 1.5},
      {"num_str", "42"},
      {"float_str", "2.5"},
      {"date_col", "2020-01-31"},
      {"date_str", "2020-01-31"},
      {"bool_col", true},
      {"bool_str", "true"},
      {"text", "abc"},
      {"indexed_num", "10"},
      {"nullable_int", 7},
      {"nullable_str", "7"},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   },
   {
      {"primaryKey", "id_1"},
      {"int_col", -7},
      {"long_col", 3},
      {"float_col", 3.0},
      {"num_str", "-7"},
      {"float_str", "1e3"},
      {"date_col", "1999-12-31"},
      {"date_str", "1999-12-31"},
      {"bool_col", false},
      {"bool_str", "false"},
      {"text", "ABC"},
      {"indexed_num", "20"},
      {"nullable_int", nullptr},
      {"nullable_str", nullptr},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   },
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "cast test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "int_col"
      type: "int32"
    - name: "long_col"
      type: "int64"
    - name: "float_col"
      type: "float"
    - name: "num_str"
      type: "string"
    - name: "float_str"
      type: "string"
    - name: "date_col"
      type: "date"
    - name: "date_str"
      type: "string"
    - name: "bool_col"
      type: "boolean"
    - name: "bool_str"
      type: "string"
    - name: "text"
      type: "string"
    - name: "indexed_num"
      type: "string"
      generateIndex: true
    - name: "nullable_int"
      type: "int32"
    - name: "nullable_str"
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
};

// ---- numeric casts on columns ----

const QueryTestScenario INT32_TO_INT64_AND_FLOAT_SCENARIO = {
   .name = "INT32_TO_INT64_AND_FLOAT",
   .query = "data.map({a := int_col::int64, b := int_col::float}).project({primaryKey, a, b})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", 42}, {"b", 42.0}},
      {{"primaryKey", "id_1"}, {"a", -7}, {"b", -7.0}},
   }),
};

const QueryTestScenario INT64_TO_INT32_AND_FLOAT_SCENARIO = {
   .name = "INT64_TO_INT32_AND_FLOAT",
   .query =
      "data.filter(primaryKey = 'id_1').map({a := long_col::int32, b := long_col::float})"
      ".project({primaryKey, a, b})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}, {"a", 3}, {"b", 3.0}}}),
};

const QueryTestScenario FLOAT_TO_INTEGER_SCENARIO = {
   .name = "FLOAT_TO_INTEGER",
   .query =
      "data.filter(primaryKey = 'id_1').map({a := float_col::int32, b := float_col::int64})"
      ".project({primaryKey, a, b})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}, {"a", 3}, {"b", 3}}}),
};

// ---- string casts on columns ----

const QueryTestScenario STRING_TO_NUMBERS_SCENARIO = {
   .name = "STRING_TO_NUMBERS",
   .query =
      "data.map({a := num_str::int32, b := num_str::int64, c := float_str::float})"
      ".project({primaryKey, a, b, c})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", 42}, {"b", 42}, {"c", 2.5}},
      {{"primaryKey", "id_1"}, {"a", -7}, {"b", -7}, {"c", 1000.0}},
   }),
};

const QueryTestScenario STRING_TO_DATE_AND_BOOLEAN_SCENARIO = {
   .name = "STRING_TO_DATE_AND_BOOLEAN",
   .query = "data.map({a := date_str::date, b := bool_str::boolean}).project({primaryKey, a, b})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", "2020-01-31"}, {"b", true}},
      {{"primaryKey", "id_1"}, {"a", "1999-12-31"}, {"b", false}},
   }),
};

const QueryTestScenario TO_STRING_SCENARIO = {
   .name = "TO_STRING",
   .query =
      "data.map({a := int_col::string, b := long_col::string, c := float_col::string,"
      " d := date_col::string, e := bool_col::string}).project({primaryKey, a, b, c, d, e})",
   .expected_query_result = nlohmann::json({
      {
         {"primaryKey", "id_0"},
         {"a", "42"},
         {"b", "5000000000"},
         {"c", "1.5"},
         {"d", "2020-01-31"},
         {"e", "true"},
      },
      {
         {"primaryKey", "id_1"},
         {"a", "-7"},
         {"b", "3"},
         {"c", "3"},
         {"d", "1999-12-31"},
         {"e", "false"},
      },
   }),
};

// ---- null handling, dictionary encoding, chaining ----

const QueryTestScenario NULL_INPUT_STAYS_NULL_SCENARIO = {
   .name = "NULL_INPUT_STAYS_NULL",
   .query =
      "data.map({a := nullable_int::float, b := nullable_str::int32, c := nullable_int::string})"
      ".project({primaryKey, a, b, c})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", 7.0}, {"b", 7}, {"c", "7"}},
      {{"primaryKey", "id_1"}, {"a", nullptr}, {"b", nullptr}, {"c", nullptr}},
   }),
};

const QueryTestScenario DICTIONARY_ENCODED_SOURCE_SCENARIO = {
   .name = "DICTIONARY_ENCODED_SOURCE",
   .query = "data.map({a := indexed_num::int32}).project({primaryKey, a})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", 10}},
      {{"primaryKey", "id_1"}, {"a", 20}},
   }),
};

const QueryTestScenario CHAINED_CASTS_SCENARIO = {
   .name = "CHAINED_CASTS",
   .query = "data.map({a := int_col::string::int64::float}).project({primaryKey, a})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", 42.0}},
      {{"primaryKey", "id_1"}, {"a", -7.0}},
   }),
};

const QueryTestScenario CAST_TO_SAME_TYPE_SCENARIO = {
   .name = "CAST_TO_SAME_TYPE",
   .query = "data.map({a := int_col::int32, b := indexed_num::string}).project({primaryKey, a, b})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", 42}, {"b", "10"}},
      {{"primaryKey", "id_1"}, {"a", -7}, {"b", "20"}},
   }),
};

const QueryTestScenario INT_IS_INT32_ALIAS_SCENARIO = {
   .name = "INT_IS_INT32_ALIAS",
   .query = "data.map({a := long_col::int}).filter(primaryKey = 'id_1').project({primaryKey, a})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}, {"a", 3}}}),
};

// ---- literal operands ----

const QueryTestScenario LITERAL_CASTS_SCENARIO = {
   .name = "LITERAL_CASTS",
   .query =
      "data.filter(primaryKey = 'id_0').map({a := '42'::int64, b := 3::float, c := 3.0::int32,"
      " d := 'true'::boolean, e := 5::string, f := '2020-01-31'::date, g := 2.5::string})"
      ".project({a, b, c, d, e, f, g})",
   .expected_query_result = nlohmann::json({
      {
         {"a", 42},
         {"b", 3.0},
         {"c", 3},
         {"d", true},
         {"e", "5"},
         {"f", "2020-01-31"},
         {"g", "2.5"},
      },
   }),
};

const QueryTestScenario TYPED_NULL_LITERAL_SCENARIO = {
   .name = "TYPED_NULL_LITERAL",
   .query =
      "data.filter(primaryKey = 'id_0').map({a := null::int32, b := null::string, c := null::date})"
      ".project({a, b, c})",
   .expected_query_result = nlohmann::json({{{"a", nullptr}, {"b", nullptr}, {"c", nullptr}}}),
};

const QueryTestScenario LITERAL_CAST_USABLE_IN_COMPARISON_SCENARIO = {
   .name = "LITERAL_CAST_USABLE_IN_COMPARISON",
   .query =
      "data.filter(int_col = '42'::int32 && date_col > '2000-01-01'::date).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

const QueryTestScenario LITERAL_CAST_FRACTION_THROWS_SCENARIO = {
   .name = "LITERAL_CAST_FRACTION_THROWS",
   .query = "data.map({a := 3.7::int32}).project({primaryKey, a})",
   .expected_error_message =
      "map() field 'a': cannot cast 3.7 from FLOAT to INT32 at 1:19: Float value 3.700000 was "
      "truncated converting to int32",
};

const QueryTestScenario LITERAL_CAST_OVERFLOW_THROWS_SCENARIO = {
   .name = "LITERAL_CAST_OVERFLOW_THROWS",
   .query = "data.map({a := 3000000000::int32}).project({primaryKey, a})",
   .expected_error_message =
      "map() field 'a': cannot cast 3000000000 from INT64 to INT32 at 1:26: Integer value "
      "3000000000 not in range: -2147483648 to 2147483647",
};

const QueryTestScenario LITERAL_CAST_NOT_A_NUMBER_THROWS_SCENARIO = {
   .name = "LITERAL_CAST_NOT_A_NUMBER_THROWS",
   .query = "data.map({a := 'abc'::int32}).project({primaryKey, a})",
   .expected_error_message =
      "map() field 'a': cannot cast 'abc' from STRING to INT32 at 1:21: Failed to parse string: "
      "'abc' as a scalar of type int32",
};

const QueryTestScenario LITERAL_CAST_BOOLEAN_SPELLING_THROWS_SCENARIO = {
   .name = "LITERAL_CAST_BOOLEAN_SPELLING_THROWS",
   .query = "data.map({a := 'TRUE'::boolean}).project({primaryKey, a})",
   .expected_error_message =
      "map() field 'a': cannot cast 'TRUE' from STRING to BOOL at 1:22: Failed to parse 'TRUE' as "
      "boolean: expected 'true' or 'false'",
};

const QueryTestScenario LITERAL_CAST_INVALID_DATE_THROWS_SCENARIO = {
   .name = "LITERAL_CAST_INVALID_DATE_THROWS",
   .query = "data.map({a := '2020-02-30'::date}).project({primaryKey, a})",
   .expected_error_message =
      "invalid date '2020-02-30' at 1:28: Invalid calendar date '2020-02-30'",
};

// ---- rejected casts ----

const QueryTestScenario UNSUPPORTED_PAIR_THROWS_SCENARIO = {
   .name = "UNSUPPORTED_PAIR_THROWS",
   .query = "data.map({a := date_col::int32}).project({primaryKey, a})",
   .expected_error_message = "map() field 'a': cannot cast from DATE32 to INT32 at 1:24",
};

const QueryTestScenario BOOLEAN_TO_INT_THROWS_SCENARIO = {
   .name = "BOOLEAN_TO_INT_THROWS",
   .query = "data.map({a := bool_col::int32}).project({primaryKey, a})",
   .expected_error_message = "map() field 'a': cannot cast from BOOL to INT32 at 1:24",
};

const QueryTestScenario UNKNOWN_TARGET_TYPE_THROWS_SCENARIO = {
   .name = "UNKNOWN_TARGET_TYPE_THROWS",
   .query = "data.map({a := int_col::banana}).project({primaryKey, a})",
   .expected_error_message =
      "cannot cast to unknown type 'banana' at 1:23; supported types are string, date, boolean, "
      "int32, int64, float",
};

const QueryTestScenario SEQUENCE_TARGET_TYPE_THROWS_SCENARIO = {
   .name = "SEQUENCE_TARGET_TYPE_THROWS",
   .query = "data.map({a := text::nucleotideSequence}).project({primaryKey, a})",
   .expected_error_message =
      "cannot cast to unknown type 'nucleotideSequence' at 1:20; supported types are string, "
      "date, boolean, int32, int64, float",
};

const QueryTestScenario UNKNOWN_COLUMN_THROWS_SCENARIO = {
   .name = "UNKNOWN_COLUMN_THROWS",
   .query = "data.map({a := nope::int32}).project({primaryKey, a})",
   .expected_error_message = "map() field 'a' references unknown column 'nope' at 1:16",
};

// ---- filters ----

const QueryTestScenario FILTER_ON_CAST_AFTER_MAP_SCENARIO = {
   .name = "FILTER_ON_CAST_AFTER_MAP",
   .query = "data.map({f := int_col::float}).filter(f < 10.0).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}}}),
};

const QueryTestScenario FILTER_ON_CAST_AFTER_LIMIT_SCENARIO = {
   .name = "FILTER_ON_CAST_AFTER_LIMIT",
   .query = "data.limit(10).filter(num_str::int32 > 0).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

const QueryTestScenario FILTER_BY_BOOLEAN_CAST_AFTER_LIMIT_SCENARIO = {
   .name = "FILTER_BY_BOOLEAN_CAST_AFTER_LIMIT",
   .query = "data.limit(10).filter(bool_str::boolean).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

const QueryTestScenario FILTER_ON_CAST_IN_TABLE_SCAN_THROWS_SCENARIO = {
   .name = "FILTER_ON_CAST_IN_TABLE_SCAN_THROWS",
   .query = "data.filter(int_col::float < 10.0).project({primaryKey})",
   .expected_error_message =
      "A Comparison expression can only be compiled to a filter when exactly one side is a column "
      "reference and the other a literal value",
};

const QueryTestScenario FILTER_BY_BOOLEAN_CAST_IN_TABLE_SCAN_THROWS_SCENARIO = {
   .name = "FILTER_BY_BOOLEAN_CAST_IN_TABLE_SCAN_THROWS",
   .query = "data.filter(bool_str::boolean).project({primaryKey})",
   .expected_error_message =
      "A type cast cannot be evaluated in a filter on a table scan. Compute the value with map() "
      "first, e.g. data.map({x := a::float}).filter(x < 3.5)",
};

const QueryTestScenario FILTER_BY_NON_BOOLEAN_CAST_THROWS_SCENARIO = {
   .name = "FILTER_BY_NON_BOOLEAN_CAST_THROWS",
   .query = "data.filter(int_col::float).project({primaryKey})",
   .expected_error_message =
      "a cast to float cannot be used as a filter predicate at 1:20; only casts to boolean can",
};

const QueryTestScenario FILTER_BY_BOOLEAN_LITERAL_CAST_SCENARIO = {
   .name = "FILTER_BY_BOOLEAN_LITERAL_CAST",
   .query = "data.filter('true'::boolean).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}, {{"primaryKey", "id_1"}}}),
};

const QueryTestScenario FILTER_BY_NULL_BOOLEAN_CAST_AFTER_LIMIT_SCENARIO = {
   .name = "FILTER_BY_NULL_BOOLEAN_CAST_AFTER_LIMIT",
   .query = "data.limit(10).filter(null::boolean).project({primaryKey})",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario FILTER_BY_NULL_BOOLEAN_CAST_IN_TABLE_SCAN_SCENARIO = {
   .name = "FILTER_BY_NULL_BOOLEAN_CAST_IN_TABLE_SCAN",
   .query = "data.filter(null::boolean).project({primaryKey})",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario FILTER_BY_NEGATED_NULL_BOOLEAN_CAST_IN_TABLE_SCAN_SCENARIO = {
   .name = "FILTER_BY_NEGATED_NULL_BOOLEAN_CAST_IN_TABLE_SCAN",
   .query = "data.filter(!null::boolean).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}, {{"primaryKey", "id_1"}}}),
};

// ---- casts of boolean expressions ----

const QueryTestScenario COMPARISON_TO_STRING_SCENARIO = {
   .name = "COMPARISON_TO_STRING",
   .query = "data.map({a := (int_col > 0)::string}).project({primaryKey, a})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", "true"}},
      {{"primaryKey", "id_1"}, {"a", "false"}},
   }),
};

const QueryTestScenario LOGICAL_EXPRESSION_TO_STRING_SCENARIO = {
   .name = "LOGICAL_EXPRESSION_TO_STRING",
   .query =
      "data.map({a := (int_col > 0 && float_col < 2.0)::string, "
      "b := (int_col < 0 || float_col < 2.0)::string}).project({primaryKey, a, b})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"a", "true"}, {"b", "true"}},
      {{"primaryKey", "id_1"}, {"a", "false"}, {"b", "true"}},
   }),
};

const QueryTestScenario COMPARISON_TO_INT_THROWS_SCENARIO = {
   .name = "COMPARISON_TO_INT_THROWS",
   .query = "data.map({a := (int_col > 0)::int32}).project({primaryKey, a})",
   .expected_error_message = "map() field 'a': cannot cast from BOOL to INT32 at 1:29",
};

// ---- column values that cannot be converted ----
//
// These casts fail while the query runs (Arrow evaluates them per batch), not at planning time.

const QueryTestScenario COLUMN_CAST_OVERFLOW_THROWS_SCENARIO = {
   .name = "COLUMN_CAST_OVERFLOW_THROWS",
   .query = "data.map({a := long_col::int32}).project({primaryKey, a})",
   .expected_error_message = "Integer value 5000000000 not in range: -2147483648 to 2147483647",
};

const QueryTestScenario COLUMN_CAST_FRACTION_THROWS_SCENARIO = {
   .name = "COLUMN_CAST_FRACTION_THROWS",
   .query = "data.map({a := float_col::int32}).project({primaryKey, a})",
   .expected_error_message = "Float value 1.500000 was truncated converting to int32",
};

const QueryTestScenario COLUMN_CAST_NOT_A_NUMBER_THROWS_SCENARIO = {
   .name = "COLUMN_CAST_NOT_A_NUMBER_THROWS",
   .query = "data.map({a := text::int32}).project({primaryKey, a})",
   .expected_error_message = "Failed to parse string: 'ABC' as a scalar of type int32",
};

const QueryTestScenario COLUMN_CAST_INVALID_DATE_THROWS_SCENARIO = {
   .name = "COLUMN_CAST_INVALID_DATE_THROWS",
   .query = "data.map({a := text::date}).project({primaryKey, a})",
   .expected_error_message = "Failed to parse string: 'ABC' as a scalar of type date32[day]",
};

const QueryTestScenario COLUMN_CAST_INVALID_BOOLEAN_THROWS_SCENARIO = {
   .name = "COLUMN_CAST_INVALID_BOOLEAN_THROWS",
   .query = "data.map({a := text::boolean}).project({primaryKey, a})",
   .expected_error_message = "Failed to parse 'abc' as boolean: expected 'true' or 'false'",
};

// ---- optimizer interplay ----

const QueryTestScenario GROUP_BY_CAST_SCENARIO = {
   .name = "GROUP_BY_CAST",
   .query =
      "data.map({k := int_col::string}).group(by:={k}, aggs:={n := count()}).order(by:={asc(k)})",
   .expected_query_result = nlohmann::json({
      {{"k", "-7"}, {"n", 1}},
      {{"k", "42"}, {"n", 1}},
   }),
};

const QueryTestScenario CAST_OF_COLUMN_FROM_PREVIOUS_MAP_SCENARIO = {
   .name = "CAST_OF_COLUMN_FROM_PREVIOUS_MAP",
   .query = "data.map({x := num_str}).map({y := x::int64}).project({primaryKey, y})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"y", 42}},
      {{"primaryKey", "id_1"}, {"y", -7}},
   }),
};

const QueryTestScenario CAST_IN_SECOND_MAP_USES_FIRST_MAP_SCENARIO = {
   .name = "CAST_IN_SECOND_MAP_USES_FIRST_MAP",
   .query = "data.map({x := concat(num_str, '0')}).map({y := x::int64}).project({primaryKey, y})",
   .expected_query_result = nlohmann::json({
      {{"primaryKey", "id_0"}, {"y", 420}},
      {{"primaryKey", "id_1"}, {"y", -70}},
   }),
};

}  // namespace

QUERY_TEST(
   CastTest,
   TEST_DATA,
   ::testing::Values(
      INT32_TO_INT64_AND_FLOAT_SCENARIO,
      INT64_TO_INT32_AND_FLOAT_SCENARIO,
      FLOAT_TO_INTEGER_SCENARIO,
      STRING_TO_NUMBERS_SCENARIO,
      STRING_TO_DATE_AND_BOOLEAN_SCENARIO,
      TO_STRING_SCENARIO,
      NULL_INPUT_STAYS_NULL_SCENARIO,
      DICTIONARY_ENCODED_SOURCE_SCENARIO,
      CHAINED_CASTS_SCENARIO,
      CAST_TO_SAME_TYPE_SCENARIO,
      INT_IS_INT32_ALIAS_SCENARIO,
      LITERAL_CASTS_SCENARIO,
      TYPED_NULL_LITERAL_SCENARIO,
      LITERAL_CAST_USABLE_IN_COMPARISON_SCENARIO,
      LITERAL_CAST_FRACTION_THROWS_SCENARIO,
      LITERAL_CAST_OVERFLOW_THROWS_SCENARIO,
      LITERAL_CAST_NOT_A_NUMBER_THROWS_SCENARIO,
      LITERAL_CAST_BOOLEAN_SPELLING_THROWS_SCENARIO,
      LITERAL_CAST_INVALID_DATE_THROWS_SCENARIO,
      UNSUPPORTED_PAIR_THROWS_SCENARIO,
      BOOLEAN_TO_INT_THROWS_SCENARIO,
      UNKNOWN_TARGET_TYPE_THROWS_SCENARIO,
      SEQUENCE_TARGET_TYPE_THROWS_SCENARIO,
      UNKNOWN_COLUMN_THROWS_SCENARIO,
      FILTER_ON_CAST_AFTER_MAP_SCENARIO,
      FILTER_ON_CAST_AFTER_LIMIT_SCENARIO,
      FILTER_BY_BOOLEAN_CAST_AFTER_LIMIT_SCENARIO,
      FILTER_ON_CAST_IN_TABLE_SCAN_THROWS_SCENARIO,
      FILTER_BY_BOOLEAN_CAST_IN_TABLE_SCAN_THROWS_SCENARIO,
      FILTER_BY_NON_BOOLEAN_CAST_THROWS_SCENARIO,
      FILTER_BY_BOOLEAN_LITERAL_CAST_SCENARIO,
      FILTER_BY_NULL_BOOLEAN_CAST_AFTER_LIMIT_SCENARIO,
      FILTER_BY_NULL_BOOLEAN_CAST_IN_TABLE_SCAN_SCENARIO,
      FILTER_BY_NEGATED_NULL_BOOLEAN_CAST_IN_TABLE_SCAN_SCENARIO,
      COMPARISON_TO_STRING_SCENARIO,
      LOGICAL_EXPRESSION_TO_STRING_SCENARIO,
      COMPARISON_TO_INT_THROWS_SCENARIO,
      COLUMN_CAST_OVERFLOW_THROWS_SCENARIO,
      COLUMN_CAST_FRACTION_THROWS_SCENARIO,
      COLUMN_CAST_NOT_A_NUMBER_THROWS_SCENARIO,
      COLUMN_CAST_INVALID_DATE_THROWS_SCENARIO,
      COLUMN_CAST_INVALID_BOOLEAN_THROWS_SCENARIO,
      GROUP_BY_CAST_SCENARIO,
      CAST_OF_COLUMN_FROM_PREVIOUS_MAP_SCENARIO,
      CAST_IN_SECOND_MAP_USES_FIRST_MAP_SCENARIO
   )
);
