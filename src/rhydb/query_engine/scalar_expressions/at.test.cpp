#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json alignedSequence(const std::string& sequence) {
   return {{"sequence", sequence}, {"insertions", nlohmann::json::array()}};
}

const std::vector<nlohmann::json> DATA = {
   {
      {"primaryKey", "id_0"},
      {"str_value", "short"},
      {"indexed_value", "B.1.1"},
      {"segment1", alignedSequence("ACGT")},
      {"gene1", nullptr},
   },
   {
      {"primaryKey", "id_1"},
      {"str_value", "longlonglong"},
      {"indexed_value", "XBB.1"},
      {"segment1", nullptr},
      {"gene1", nullptr},
   },
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "at test"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "str_value"
      type: "string"
    - name: "indexed_value"
      type: "string"
      generateIndex: true
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
   .without_unaligned_sequences = true,
};

const QueryTestScenario AT_STRING_SCENARIO = {
   .name = "AT_STRING",
   .query = "data.map({second := primaryKey.at(4)}).project({primaryKey, second})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"second", "0"}}, {{"primaryKey", "id_1"}, {"second", "1"}}}
   ),
};

// The square-bracket notation `col[i]` is shorthand for `col.at(i)`.
const QueryTestScenario AT_BRACKET_SCENARIO = {
   .name = "AT_BRACKET",
   .query = "data.map({second := primaryKey[4]}).project({primaryKey, second})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"second", "0"}}, {{"primaryKey", "id_1"}, {"second", "1"}}}
   ),
};

const QueryTestScenario AT_STRING_OUT_OF_BOUNDS_SCENARIO = {
   .name = "AT_STRING_OUT_OF_BOUNDS",
   .query = "data.map({eighth := str_value.at(8)}).project({primaryKey, eighth})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"eighth", ""}}, {{"primaryKey", "id_1"}, {"eighth", "g"}}}
   ),
};

// An indexed string column is dictionary-encoded, but still a valid string input to `.at()`.
const QueryTestScenario AT_DICTIONARY_ENCODED_SCENARIO = {
   .name = "AT_DICTIONARY_ENCODED",
   .query = "data.map({second := indexed_value.at(2)}).project({primaryKey, second})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"second", "."}}, {{"primaryKey", "id_1"}, {"second", "B"}}}
   ),
};

const QueryTestScenario AT_SEQUENCE_FIRST_SCENARIO = {
   .name = "AT_SEQUENCE_FIRST",
   .query = "data.map({base := segment1.at(1)}).project({primaryKey, base})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"base", "A"}}, {{"primaryKey", "id_1"}, {"base", nullptr}}}
   ),
};

const QueryTestScenario AT_SEQUENCE_INNER_SCENARIO = {
   .name = "AT_SEQUENCE_INNER",
   .query = "data.map({base := segment1.at(3)}).project({primaryKey, base})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"base", "G"}}, {{"primaryKey", "id_1"}, {"base", nullptr}}}
   ),
};

// A sequence `.at()` comparison applied directly to the table scan is rewritten into the symbol
// filter that `nucleotideEquals` uses, so it is answered from the sequence index.
const QueryTestScenario AT_FILTER_ON_SCAN_SCENARIO = {
   .name = "AT_FILTER_ON_SCAN",
   .query = "data.filter(segment1.at(1) = 'A').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

// The same `.at()` predicate placed above a limit() cannot be pushed into the scan (limit is a
// pushdown barrier), so it is retained and executed as an Arrow filter over the `.at()` value.
const QueryTestScenario AT_FILTER_ABOVE_LIMIT_SCENARIO = {
   .name = "AT_FILTER_ABOVE_LIMIT",
   .query = "data.limit(2).filter(segment1.at(1) = 'A').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

}  // namespace

QUERY_TEST(
   AtTest,
   TEST_DATA,
   ::testing::Values(
      AT_STRING_SCENARIO,
      AT_BRACKET_SCENARIO,
      AT_STRING_OUT_OF_BOUNDS_SCENARIO,
      AT_DICTIONARY_ENCODED_SCENARIO,
      AT_SEQUENCE_FIRST_SCENARIO,
      AT_SEQUENCE_INNER_SCENARIO,
      AT_FILTER_ON_SCAN_SCENARIO,
      AT_FILTER_ABOVE_LIMIT_SCENARIO
   )
);

namespace {

const std::vector<nlohmann::json> FILTER_DATA = {
   {
      {"primaryKey", "id_0"},
      {"segment1", alignedSequence("ACGT")},
      {"gene1", alignedSequence("*")},
   },
   {
      {"primaryKey", "id_1"},
      {"segment1", nullptr},
      {"gene1", nullptr},
   },
   {
      {"primaryKey", "id_2"},
      {"segment1", alignedSequence("NCGT")},
      {"gene1", alignedSequence("X")},
   },
   {
      {"primaryKey", "id_3"},
      {"segment1", alignedSequence("RCGA")},
      {"gene1", nullptr},
   },
};

const auto FILTER_DATABASE_CONFIG =
   R"(
schema:
  instanceName: "at filter test"
  metadata:
    - name: "primaryKey"
      type: "string"
  primaryKey: "primaryKey"
)";

const QueryTestData FILTER_TEST_DATA{
   .ndjson_input_data = FILTER_DATA,
   .database_config = FILTER_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

nlohmann::json primaryKeys(const std::vector<std::string>& keys) {
   auto result = nlohmann::json::array();
   for (const auto& key : keys) {
      result.push_back({{"primaryKey", key}});
   }
   return result;
}

/// Directly on the table scan the predicate is rewritten into a sequence-index filter, above
/// limit() (a pushdown barrier) it is evaluated by Arrow on the extracted character. Both must
/// select the same rows, so every predicate is checked in both positions.
std::vector<QueryTestScenario> onScanAndAboveLimit(
   const std::string& name,
   const std::string& predicate,
   const std::vector<std::string>& expected_keys
) {
   return {
      {
         .name = name + "_ON_SCAN",
         .query = fmt::format("data.filter({}).project({{primaryKey}})", predicate),
         .expected_query_result = primaryKeys(expected_keys),
      },
      {
         .name = name + "_ABOVE_LIMIT",
         .query = fmt::format("data.limit(10).filter({}).project({{primaryKey}})", predicate),
         .expected_query_result = primaryKeys(expected_keys),
      },
   };
}

std::vector<QueryTestScenario> filterScenarios() {
   std::vector<QueryTestScenario> scenarios;
   const auto add = [&](
                       const std::string& name,
                       const std::string& predicate,
                       const std::vector<std::string>& expected_keys
                    ) {
      std::ranges::move(
         onScanAndAboveLimit(name, predicate, expected_keys), std::back_inserter(scenarios)
      );
   };
   add("EQUALS_MUTATED", "segment1[1] = 'A'", {"id_0"});
   add("EQUALS_LITERAL_ON_LEFT", "'A' = segment1[1]", {"id_0"});
   add("EQUALS_REFERENCE", "segment1[4] = 'T'", {"id_0", "id_2"});
   add("EQUALS_MISSING_EXCLUDES_NULL", "segment1[1] = 'N'", {"id_2"});
   add("EQUALS_AMBIGUOUS", "segment1[1] = 'R'", {"id_3"});
   add("EQUALS_LOWER_CASE", "segment1[1] = 'a'", {});
   add("EQUALS_MULTIPLE_CHARACTERS", "segment1[1] = 'AC'", {});
   add("EQUALS_INVALID_SYMBOL", "segment1[1] = 'X'", {});
   add("EQUALS_OUT_OF_BOUNDS", "segment1[5] = 'A'", {});
   add("NOT_EQUALS", "segment1[1] <> 'A'", {"id_2", "id_3"});
   add("NOT_EQUALS_REFERENCE", "segment1[4] <> 'T'", {"id_3"});
   add("NOT_EQUALS_INVALID_SYMBOL", "segment1[1] <> 'X'", {"id_0", "id_2", "id_3"});
   add("NOT_EQUALS_OUT_OF_BOUNDS", "segment1[5] <> 'A'", {"id_0", "id_2", "id_3"});
   add("EQUALS_EMPTY_OUT_OF_BOUNDS", "segment1[5] = ''", {"id_0", "id_2", "id_3"});
   add("NOT_EQUALS_EMPTY_OUT_OF_BOUNDS", "segment1[5] <> ''", {});
   // A comparison that can never match is still null on a null sequence, so its negation must not
   // select id_1.
   add("NEGATED_EQUALS_OUT_OF_BOUNDS", "!(segment1[5] = 'A')", {"id_0", "id_2", "id_3"});
   add("NEGATED_EQUALS_INVALID_SYMBOL", "!(segment1[1] = 'X')", {"id_0", "id_2", "id_3"});
   add("NEGATED_NOT_EQUALS_EMPTY_OUT_OF_BOUNDS", "!(segment1[5] <> '')", {"id_0", "id_2", "id_3"});
   // Likewise, a comparison that always matches is null, not true, on a null sequence.
   add("NEGATED_NOT_EQUALS", "!(segment1[1] <> 'A')", {"id_0"});
   add("NEGATED_NOT_EQUALS_OUT_OF_BOUNDS", "!(segment1[5] <> 'A')", {});
   add("NEGATED_EQUALS_EMPTY_OUT_OF_BOUNDS", "!(segment1[5] = '')", {});
   add("AMINO_ACID_EQUALS", "gene1[1] = '*'", {"id_0"});
   add("AMINO_ACID_EQUALS_ANY", "gene1[1] = 'X'", {"id_2"});
   add("AMINO_ACID_NOT_EQUALS", "gene1[1] <> '*'", {"id_2"});
   add("IN_OR", "segment1[1] = 'A' || segment1[4] = 'A'", {"id_0", "id_3"});

   // Like nucleotideEquals, the index filter honours maybe(): ambiguous symbols may be 'A'.
   scenarios.push_back({
      .name = "MAYBE_EQUALS_ON_SCAN",
      .query = "data.filter(maybe(segment1[1] = 'A')).project({primaryKey})",
      .expected_query_result = primaryKeys({"id_0", "id_2", "id_3"}),
   });
   scenarios.push_back({
      .name = "MAYBE_NOT_EQUALS_ON_SCAN",
      .query = "data.filter(maybe(segment1[1] <> 'A')).project({primaryKey})",
      .expected_query_result = primaryKeys({"id_2", "id_3"}),
   });
   return scenarios;
}

}  // namespace

QUERY_TEST(AtFilterTest, FILTER_TEST_DATA, ::testing::ValuesIn(filterScenarios()));
