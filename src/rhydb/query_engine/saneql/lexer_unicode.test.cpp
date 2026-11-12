#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const auto DATABASE_CONFIG = R"(
schema:
  instanceName: "test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "country"
      type: "string"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "A"}},
   {{"gene1", "*"}},
};

nlohmann::json createData(const std::string& primary_key, const std::string& country) {
   return {
      {"primaryKey", primary_key},
      {"country", country},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

const std::vector<nlohmann::json> DATA = {
   createData("id_0", "CH"),
   createData("id_1", "Côte d'Ivoire"),
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario NON_BREAKING_SPACE_IS_WHITESPACE = {
   .name = "NON_BREAKING_SPACE_IS_WHITESPACE",
   .query = "data.filter(country\xC2\xA0='CH').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

const QueryTestScenario NON_ASCII_CHARACTER_OUTSIDE_LITERAL_IS_REJECTED = {
   .name = "NON_ASCII_CHARACTER_OUTSIDE_LITERAL_IS_REJECTED",
   .query = "data.filter(country='CH').project({primaryKey\xC3\xA9})",
   .expected_error_message = "Parse error at 1:46: Unexpected character U+00E9",
};

const QueryTestScenario INVALID_UTF8_OUTSIDE_LITERAL_IS_REJECTED = {
   .name = "INVALID_UTF8_OUTSIDE_LITERAL_IS_REJECTED",
   .query = "data.filter(country='CH').project({primaryKey\xC3})",
   .expected_error_message = "Parse error at 1:46: Invalid UTF-8 byte 0xC3",
};

const QueryTestScenario NON_ASCII_STRING_LITERAL_IS_SUPPORTED = {
   .name = "NON_ASCII_STRING_LITERAL_IS_SUPPORTED",
   .query = "data.filter(country='Côte d''Ivoire').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}}}),
};

}  // namespace

QUERY_TEST(
   LexerUnicodeTest,
   TEST_DATA,
   ::testing::Values(
      NON_BREAKING_SPACE_IS_WHITESPACE,
      NON_ASCII_CHARACTER_OUTSIDE_LITERAL_IS_REJECTED,
      INVALID_UTF8_OUTSIDE_LITERAL_IS_REJECTED,
      NON_ASCII_STRING_LITERAL_IS_SUPPORTED
   )
)
