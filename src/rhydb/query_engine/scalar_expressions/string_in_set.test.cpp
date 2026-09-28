#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(const std::string& primary_key, const std::string& country) {
   return nlohmann::json::parse(fmt::format(
      R"(
{{
   "primaryKey": "{}",
   "country": "{}",
   "region": "{}",
   "segment1": {{
      "sequence": "ACGT",
      "insertions": []
   }}
}}
)",
      primary_key,
      country,
      country == "USA" ? "Americas" : "Europe"
   ));
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "country"
     type: "string"
   - name: "region"
     type: "string"
     generateIndex: true
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ATGCN"}},
   {},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {createData("id_0", "Switzerland"),
       createData("id_1", "Germany"),
       createData("id_2", "USA"),
       createData("id_3", "Switzerland"),
       createData("id_4", "France"),
       createData("id_5", "Germany"),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

const QueryTestScenario STRING_IN_SET_SINGLE_VALUE = {
   .name = "STRING_IN_SET_SINGLE_VALUE",
   .query = "default.filter(country.in({'Switzerland'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0"},{"country":"Switzerland","primaryKey":"id_3"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_MULTIPLE_VALUES = {
   .name = "STRING_IN_SET_MULTIPLE_VALUES",
   .query = "default.filter(country.in({'Switzerland', 'Germany'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0"},{"country":"Germany","primaryKey":"id_1"},{"country":"Switzerland","primaryKey":"id_3"},{"country":"Germany","primaryKey":"id_5"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_NO_MATCH = {
   .name = "STRING_IN_SET_NO_MATCH",
   .query = "default.filter(country.in({'Japan', 'China'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

const QueryTestScenario STRING_IN_SET_EMPTY_VALUES = {
   .name = "STRING_IN_SET_EMPTY_VALUES",
   .query = "default.filter(country.in({})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

const QueryTestScenario STRING_IN_SET_INDEXED_COLUMN = {
   .name = "STRING_IN_SET_INDEXED_COLUMN",
   .query = "default.filter(region.in({'Europe'})).project({primaryKey, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_0","region":"Europe"},{"primaryKey":"id_1","region":"Europe"},{"primaryKey":"id_3","region":"Europe"},{"primaryKey":"id_4","region":"Europe"},{"primaryKey":"id_5","region":"Europe"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_WITH_AND = {
   .name = "STRING_IN_SET_WITH_AND",
   .query =
      "default.filter(country.in({'Switzerland', 'Germany', 'France'}) && region = "
      "'Europe').project({primaryKey, country, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0","region":"Europe"},{"country":"Germany","primaryKey":"id_1","region":"Europe"},{"country":"Switzerland","primaryKey":"id_3","region":"Europe"},{"country":"France","primaryKey":"id_4","region":"Europe"},{"country":"Germany","primaryKey":"id_5","region":"Europe"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_NEGATED = {
   .name = "STRING_IN_SET_NEGATED",
   .query =
      "default.filter(!(country.in({'Switzerland', 'Germany'}))).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"USA","primaryKey":"id_2"},{"country":"France","primaryKey":"id_4"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_INVALID_COLUMN_TYPE_INTEGER = {
   .name = "STRING_IN_SET_INVALID_COLUMN_TYPE_INTEGER",
   .query = "default.filter(in(123, {'Switzerland'}))",
   .expected_error_message = "expected identifier at 1:19",
};

const QueryTestScenario STRING_IN_SET_INVALID_COLUMN_TYPE_STRING = {
   .name = "STRING_IN_SET_INVALID_COLUMN_TYPE_STRING",
   .query = "default.filter('string'.in({'Switzerland'}))",
   .expected_error_message = "expected identifier at 1:16",
};

const QueryTestScenario STRING_IN_SET_INVALID_VALUES_TYPE = {
   .name = "STRING_IN_SET_INVALID_VALUES_TYPE",
   .query = "default.filter(country.in('Switzerland'))",
   .expected_error_message = "in() expects a set literal argument at 1:27",
};

const QueryTestScenario STRING_IN_SET_NONEXISTENT_COLUMN = {
   .name = "STRING_IN_SET_NONEXISTENT_COLUMN",
   .query = "default.filter(nonexistent.in({'Switzerland'}))",
   .expected_error_message = "The database does not contain the column 'nonexistent'",
};

}  // namespace

QUERY_TEST(
   StringInSet,
   TEST_DATA,
   ::testing::Values(
      STRING_IN_SET_SINGLE_VALUE,
      STRING_IN_SET_MULTIPLE_VALUES,
      STRING_IN_SET_NO_MATCH,
      STRING_IN_SET_EMPTY_VALUES,
      STRING_IN_SET_INDEXED_COLUMN,
      STRING_IN_SET_WITH_AND,
      STRING_IN_SET_NEGATED,
      STRING_IN_SET_INVALID_COLUMN_TYPE_INTEGER,
      STRING_IN_SET_INVALID_COLUMN_TYPE_STRING,
      STRING_IN_SET_INVALID_VALUES_TYPE,
      STRING_IN_SET_NONEXISTENT_COLUMN
   )
);
