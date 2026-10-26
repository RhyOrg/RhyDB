#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(const std::string& primary_key, const std::optional<std::string>& date) {
   return {
      {"primaryKey", primary_key},
      {"indexed_date", date.has_value() ? nlohmann::json(date.value()) : nlohmann::json(nullptr)},
   };
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "indexed_date"
      type: "date"
      generateIndex: true
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

// 1970-01-01 is the default-constructed date, which the dictionary also uses as the placeholder
// value of null rows, so it must stay distinguishable from null.
const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_0", "2021-03-01"),
         createData("id_1", "2021-01-15"),
         createData("id_2", std::nullopt),
         createData("id_3", "2021-03-01"),
         createData("id_4", "2020-12-24"),
         createData("id_5", "1970-01-01"),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario PROJECT = {
   .name = "PROJECT",
   .query =
      "data.filter(primaryKey = 'id_1' || primaryKey = 'id_2').project({primaryKey, "
      "indexed_date})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_1","indexed_date":"2021-01-15"},
          {"primaryKey":"id_2","indexed_date":null}])"
   ),
};

const QueryTestScenario EQUALS = {
   .name = "EQUALS",
   .query = "data.filter(indexed_date = '2021-03-01'::date).project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"}])"),
};

const QueryTestScenario EQUALS_ABSENT_VALUE = {
   .name = "EQUALS_ABSENT_VALUE",
   .query = "data.filter(indexed_date = '2021-03-02'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario EQUALS_NULL_PLACEHOLDER = {
   .name = "EQUALS_NULL_PLACEHOLDER",
   .query = "data.filter(indexed_date = '1970-01-01'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_5"}])"),
};

const QueryTestScenario NOT_EQUALS = {
   .name = "NOT_EQUALS",
   .query = "data.filter(indexed_date <> '2021-03-01'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_1"},{"primaryKey":"id_4"},{"primaryKey":"id_5"}])"
   ),
};

const QueryTestScenario LESS_THAN = {
   .name = "LESS_THAN",
   .query = "data.filter(indexed_date < '2021-03-01'::date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_1"},{"primaryKey":"id_4"},{"primaryKey":"id_5"}])"
   ),
};

const QueryTestScenario FLIPPED_LESS_EQUAL = {
   .name = "FLIPPED_LESS_EQUAL",
   .query = "data.filter('2021-01-15'::date <= indexed_date).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_0"},{"primaryKey":"id_1"},{"primaryKey":"id_3"}])"
   ),
};

const QueryTestScenario BETWEEN = {
   .name = "BETWEEN",
   .query =
      "data.filter(indexed_date.between('2020-12-24'::date, '2021-01-15'::date))"
      ".project(primaryKey)",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_1"},{"primaryKey":"id_4"}])"),
};

const QueryTestScenario BETWEEN_WITHOUT_MATCH = {
   .name = "BETWEEN_WITHOUT_MATCH",
   .query =
      "data.filter(indexed_date.between('2022-01-01'::date, '2022-12-31'::date))"
      ".project(primaryKey)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario IS_NULL = {
   .name = "IS_NULL",
   .query = "data.filter(indexed_date.isNull()).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_2"}])"),
};

const QueryTestScenario GROUP_BY = {
   .name = "GROUP_BY",
   .query = "data.group(by:={indexed_date}, aggs:={count:=count()}).order(by:={indexed_date})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"indexed_date":null,"count":1},
          {"indexed_date":"1970-01-01","count":1},
          {"indexed_date":"2020-12-24","count":1},
          {"indexed_date":"2021-01-15","count":1},
          {"indexed_date":"2021-03-01","count":2}])"
   ),
};

const QueryTestScenario STRING_LITERAL = {
   .name = "STRING_LITERAL",
   .query = "data.filter(indexed_date = '2021-03-01').project(primaryKey)",
   .expected_error_message = "The column 'indexed_date' is not of type string",
};

}  // namespace

QUERY_TEST(
   DictionaryEncodedDateTest,
   TEST_DATA,
   ::testing::Values(
      PROJECT,
      EQUALS,
      EQUALS_ABSENT_VALUE,
      EQUALS_NULL_PLACEHOLDER,
      NOT_EQUALS,
      LESS_THAN,
      FLIPPED_LESS_EQUAL,
      BETWEEN,
      BETWEEN_WITHOUT_MATCH,
      IS_NULL,
      GROUP_BY,
      STRING_LITERAL
   )
);
