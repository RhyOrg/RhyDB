#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

// String ordering must use unsigned byte comparison so that bytes >= 0x80 (here the
// UTF-8 encoding of 'a-umlaut', 0xC3 0xA4) sort *after* ASCII, consistently across
// the short-string, long-string suffix-fallback, and dictionary comparison paths.
// With a signed `char` comparison these bytes would sort before ASCII, and the three
// paths would disagree.
const std::string SHORT_NON_ASCII = "\xC3\xA4";  // "a-umlaut", 2 bytes, stored in-place

// Two strings longer than the 12-byte inline limit that share their first 12 bytes,
// so the prefix cannot decide the comparison and the suffix fall-back path runs. They
// differ only at index 12: 'z' (0x7A) versus the 0xC3 lead byte of "a-umlaut".
const std::string LONG_ASCII = "prefixaaaaaaz";             // 13 bytes
const std::string LONG_NON_ASCII = "prefixaaaaaa\xC3\xA4";  // 14 bytes

nlohmann::json createData(const std::string& primary_key, const std::string& value) {
   return {{"primaryKey", primary_key}, {"stringField", value}, {"dictField", value}};
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
      {createData("id_z", "z"),
       createData("id_ae", SHORT_NON_ASCII),
       createData("id_long_ascii", LONG_ASCII),
       createData("id_long_non_ascii", LONG_NON_ASCII),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// --- short-string path: 'a-umlaut' (0xC3..) must be greater than 'z' (0x7A) ---

const QueryTestScenario SHORT_GREATER_THAN_ASCII = {
   .name = "STRING_SHORT_NON_ASCII_GREATER_THAN_ASCII",
   .query = "default.filter(stringField > 'z').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_ae"}])"),
};

const QueryTestScenario DICT_SHORT_GREATER_THAN_ASCII = {
   .name = "DICT_SHORT_NON_ASCII_GREATER_THAN_ASCII",
   .query = "default.filter(dictField > 'z').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_ae"}])"),
};

// --- long-string suffix fall-back path: id_long_non_ascii proves the fix ---

const QueryTestScenario LONG_FALLBACK_GREATER = {
   .name = "STRING_LONG_NON_ASCII_SUFFIX_FALLBACK",
   .query = "default.filter(stringField > 'prefixaaaaaaz').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_z"},{"primaryKey":"id_ae"},{"primaryKey":"id_long_non_ascii"}])"
   ),
};

const QueryTestScenario DICT_LONG_GREATER = {
   .name = "DICT_LONG_NON_ASCII",
   .query = "default.filter(dictField > 'prefixaaaaaaz').project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_z"},{"primaryKey":"id_ae"},{"primaryKey":"id_long_non_ascii"}])"
   ),
};

}  // namespace

QUERY_TEST(
   StringComparisonUnsignedTest,
   TEST_DATA,
   ::testing::Values(
      SHORT_GREATER_THAN_ASCII,
      DICT_SHORT_GREATER_THAN_ASCII,
      LONG_FALLBACK_GREATER,
      DICT_LONG_GREATER
   )
);
