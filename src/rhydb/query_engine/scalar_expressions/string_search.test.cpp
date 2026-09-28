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
   .query = "default.filter(test_column.like('AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_AA_AT_THE_BEGINNING = {
   .name = "FILTER_FOR_AA_AT_THE_BEGINNING",
   .query = "default.filter(test_column.like('^AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR = {
   .name = "FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR",
   .query = "default.filter(test_column.like('should not match on anything')).project(primaryKey)",
   .expected_query_result = createExpectedResult({}),
};

const QueryTestScenario FILTER_FOR_AA_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_AA_ON_INDEXED_COLUMN",
   .query = "default.filter(indexed_test_column.like('AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id2", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_AA_AT_THE_BEGINNING_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_AA_AT_THE_BEGINNING_ON_INDEXED_COLUMN",
   .query = "default.filter(indexed_test_column.like('^AA')).project(primaryKey)",
   .expected_query_result = createExpectedResult({"id1", "id3", "id5"}),
};

const QueryTestScenario FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR_ON_INDEXED_COLUMN = {
   .name = "FILTER_FOR_SOMETHING_THAT_DOES_NOT_OCCUR_ON_INDEXED_COLUMN",
   .query =
      "default.filter(indexed_test_column.like('should not match on anything'))"
      ".project(primaryKey)",
   .expected_query_result = createExpectedResult({}),
};

const QueryTestScenario INVALID_REGULAR_EXPRESSION = {
   .name = "INVALID_REGULAR_EXPRESSION",
   .query = "default.filter(test_column.like('^(')).project(primaryKey)",
   .expected_error_message =
      "Invalid Regular Expression. The parsing of the regular expression failed with the error "
      "'missing ): ^('. See https://github.com/google/re2/wiki/Syntax for a Syntax specification.",
};

const QueryTestScenario FILTER_FOR_COLUMN_THAT_DOES_NOT_EXIST = {
   .name = "FILTER_FOR_COLUMN_THAT_DOES_NOT_EXIST",
   .query = "default.filter(column_that_does_not_exist.like('some value')).project(primaryKey)",
   .expected_error_message = "The database does not contain the column 'column_that_does_not_exist'",
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
      INVALID_REGULAR_EXPRESSION,
      FILTER_FOR_COLUMN_THAT_DOES_NOT_EXIST,
      TABLE_NOT_FOUND
   )
);
