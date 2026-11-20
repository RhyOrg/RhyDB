#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

namespace {

const std::string TEST_COLUMN = "test_column";
const std::string INDEXED_TEST_COLUMN = "indexed_test_column";

nlohmann::json createDataEntry(std::string primary_key, nlohmann::json test_column_value) {
   return {
      {"primaryKey", primary_key},
      {TEST_COLUMN, test_column_value},
      {INDEXED_TEST_COLUMN, test_column_value},
      {"float_value", nullptr},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

const std::vector<nlohmann::json> DATA = {
   createDataEntry("id1", "AA"),
   createDataEntry("id2", "BAA"),
   createDataEntry("id3", "AAB"),
   createDataEntry("id4", "ABA"),
   createDataEntry("id5", "AA"),
   createDataEntry("id6", "something else"),
   createDataEntry("id7", nullptr),
   createDataEntry("id8", ""),
};

const auto DATABASE_CONFIG = fmt::format(
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "{}"
      type: "string"
    - name: "{}"
      type: "string"
      generateIndex: true
  primaryKey: "primaryKey"
)",
   TEST_COLUMN,
   INDEXED_TEST_COLUMN
);

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "A"}},
   {{"gene1", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

nlohmann::json createExpectedResult(const std::vector<std::string>& primary_keys) {
   nlohmann::json result = nlohmann::json::array();
   for (const auto& primary_key : primary_keys) {
      result.push_back({{"primaryKey", primary_key}});
   }
   return result;
}

const QueryTestScenario FILTER_FOR_AA = {
   .name = "FILTER_FOR_AA",
   .query = "data.filter(test_column.like('AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_AA_AT_THE_BEGINNING = {
   .name = "FILTER_FOR_AA_AT_THE_BEGINNING",
   .query = "data.filter(test_column.like('^AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR = {
   .name = "FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR",
   .query = "data.filter(test_column.like('should not match on anything')).project(primaryKey)",
   .expected_query_result = createExpectedResult({}),
};

const QueryTestScenario FILTER_FOR_AA_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_AA_ON_INDEXED_COLUMN",
   .query = "data.filter(indexed_test_column.like('AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_AA_AT_THE_BEGINNING_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_AA_AT_THE_BEGINNING_ON_INDEXED_COLUMN",
   .query = "data.filter(indexed_test_column.like('^AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR_ON_INDEXED_COLUMN",
   .query =
      "data.filter(indexed_test_column.like('should not match on anything'))"
      ".project(primaryKey)",
   .expected_query_result = createExpectedResult({}),
};

const QueryTestScenario FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL = {
   .name = "FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL",
   .query = "data.filter(test_column.like('^$')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id8"}),
};

const QueryTestScenario FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN",
   .query = "data.filter(indexed_test_column.like('^$')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id8"}),
};

const QueryTestScenario FILTER_FOR_ANYTHING_DOES_NOT_MATCH_NULL = {
   .name = "FILTER_FOR_ANYTHING_DOES_NOT_MATCH_NULL",
   .query = "data.filter(test_column.like('')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id4", "id5", "id6", "id8"}),
};

const QueryTestScenario FILTER_FOR_ANYTHING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_ANYTHING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN",
   .query = "data.filter(indexed_test_column.like('')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id4", "id5", "id6", "id8"}),
};

// A search on a null value is null, so its negation is null as well and does not select the row
const QueryTestScenario NEGATED_FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL = {
   .name = "NEGATED_FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL",
   .query = "data.filter(!(test_column.like('^$'))).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id4", "id5", "id6"}),
};

const QueryTestScenario NEGATED_FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN = {
   .name = "NEGATED_FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN",
   .query = "data.filter(!(indexed_test_column.like('^$'))).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id4", "id5", "id6"}),
};

// null && true is null, so the negation does not select the null row
const QueryTestScenario NEGATED_CONJUNCTION_WITH_IS_NULL = {
   .name = "NEGATED_CONJUNCTION_WITH_IS_NULL",
   .query = "data.filter(!(test_column.like('AA') && test_column.isNull())).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id4", "id5", "id6", "id8"}),
};

// null || true is true, so the negation does not select the null row
const QueryTestScenario NEGATED_DISJUNCTION_WITH_IS_NULL = {
   .name = "NEGATED_DISJUNCTION_WITH_IS_NULL",
   .query = "data.filter(!(test_column.like('AA') || test_column.isNull())).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id4", "id6", "id8"}),
};

// The scenarios below put the search into a `map`, which is never pushed into the table scan, so
// the expression is evaluated by Arrow (`toArrowExpression`) and not by the bitmap-based `compile`.
// The rows are id1..id8 with the values of `DATA`; id7 is null.
nlohmann::json createMapResult(const std::vector<std::optional<bool>>& values) {
   nlohmann::json result = nlohmann::json::array();
   for (size_t i = 0; i < values.size(); ++i) {
      result.push_back({
         {"primaryKey", "id" + std::to_string(i + 1)},
         {"r", values[i].has_value() ? nlohmann::json(*values[i]) : nlohmann::json(nullptr)},
      });
   }
   return result;
}

constexpr std::optional<bool> NUL = std::nullopt;

QueryTestScenario mapScenario(
   const std::string& name,
   const std::string& column,
   const std::string& pattern,
   const std::vector<std::optional<bool>>& expected
) {
   return {
      .name = name,
      .query = "data.map({r := " + column + ".like('" + pattern + "')}).project({primaryKey, r})",
      .expected_query_result = createMapResult(expected),
   };
}

const QueryTestScenario ARROW_SEARCH_UNANCHORED = mapScenario(
   "ARROW_SEARCH_UNANCHORED",
   TEST_COLUMN,
   "AA",
   {true, true, true, false, true, false, NUL, false}
);
const QueryTestScenario ARROW_SEARCH_ANCHORED_AT_THE_BEGINNING = mapScenario(
   "ARROW_SEARCH_ANCHORED_AT_THE_BEGINNING",
   TEST_COLUMN,
   "^AA",
   {true, false, true, false, true, false, NUL, false}
);
const QueryTestScenario ARROW_SEARCH_EMPTY_STRING = mapScenario(
   "ARROW_SEARCH_EMPTY_STRING",
   TEST_COLUMN,
   "^$",
   {false, false, false, false, false, false, NUL, true}
);
const QueryTestScenario ARROW_SEARCH_EMPTY_PATTERN_MATCHES_EVERYTHING_BUT_NULL = mapScenario(
   "ARROW_SEARCH_EMPTY_PATTERN_MATCHES_EVERYTHING_BUT_NULL",
   TEST_COLUMN,
   "",
   {true, true, true, true, true, true, NUL, true}
);
const QueryTestScenario ARROW_SEARCH_NO_MATCH = mapScenario(
   "ARROW_SEARCH_NO_MATCH",
   TEST_COLUMN,
   "should not match on anything",
   {false, false, false, false, false, false, NUL, false}
);
const QueryTestScenario ARROW_SEARCH_CHARACTER_CLASS = mapScenario(
   "ARROW_SEARCH_CHARACTER_CLASS",
   TEST_COLUMN,
   "^A[AB]+$",
   {true, false, true, true, true, false, NUL, false}
);
const QueryTestScenario ARROW_SEARCH_ON_INDEXED_COLUMN = mapScenario(
   "ARROW_SEARCH_ON_INDEXED_COLUMN",
   INDEXED_TEST_COLUMN,
   "AA",
   {true, true, true, false, true, false, NUL, false}
);
const QueryTestScenario ARROW_SEARCH_EMPTY_STRING_ON_INDEXED_COLUMN = mapScenario(
   "ARROW_SEARCH_EMPTY_STRING_ON_INDEXED_COLUMN",
   INDEXED_TEST_COLUMN,
   "^$",
   {false, false, false, false, false, false, NUL, true}
);
const QueryTestScenario INVALID_REGULAR_EXPRESSION = {
   .name = "INVALID_REGULAR_EXPRESSION",
   .query = "data.filter(test_column.like('^(')).project(primaryKey)",
   .expected_error_message =
      "Invalid Regular Expression. The parsing of the regular expression failed with the error "
      "'missing ): ^('. See https://github.com/google/re2/wiki/Syntax for a Syntax specification.",
};

const QueryTestScenario FILTER_FOR_COLUMN_THAT_DOES_NOT_EXIST = {
   .name = "FILTER_FOR_COLUMN_THAT_DOES_NOT_EXIST",
   .query = "data.filter(column_that_does_not_exist.like('some value')).project(primaryKey)",
   .expected_error_message =
      "The database does not contain the column 'column_that_does_not_exist'",
};

const QueryTestScenario TABLE_NOT_FOUND = {
   .name = "TABLE_NOT_FOUND",
   .query = "nonexistent.filter(test_column.like('AA')).project(primaryKey)",
   .expected_error_message = "table 'nonexistent' not found in database",
};

}  // namespace

QUERY_TEST(
   StringSearchTest,
   TEST_DATA,
   ::testing::Values(
      FILTER_FOR_AA,
      FILTER_FOR_AA_AT_THE_BEGINNING,
      FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR,
      FILTER_FOR_AA_ON_INDEXED_COLUMN,
      FILTER_FOR_AA_AT_THE_BEGINNING_ON_INDEXED_COLUMN,
      FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR_ON_INDEXED_COLUMN,
      FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL,
      FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN,
      FILTER_FOR_ANYTHING_DOES_NOT_MATCH_NULL,
      FILTER_FOR_ANYTHING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN,
      NEGATED_FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL,
      NEGATED_FILTER_FOR_EMPTY_STRING_DOES_NOT_MATCH_NULL_ON_INDEXED_COLUMN,
      NEGATED_CONJUNCTION_WITH_IS_NULL,
      NEGATED_DISJUNCTION_WITH_IS_NULL,
      INVALID_REGULAR_EXPRESSION,
      FILTER_FOR_COLUMN_THAT_DOES_NOT_EXIST,
      TABLE_NOT_FOUND,
      ARROW_SEARCH_UNANCHORED,
      ARROW_SEARCH_ANCHORED_AT_THE_BEGINNING,
      ARROW_SEARCH_EMPTY_STRING,
      ARROW_SEARCH_EMPTY_PATTERN_MATCHES_EVERYTHING_BUT_NULL,
      ARROW_SEARCH_NO_MATCH,
      ARROW_SEARCH_CHARACTER_CLASS,
      ARROW_SEARCH_ON_INDEXED_COLUMN,
      ARROW_SEARCH_EMPTY_STRING_ON_INDEXED_COLUMN
   )
);
