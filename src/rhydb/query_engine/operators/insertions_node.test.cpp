#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const nlohmann::json::array_t& segment1_insertions,
   const nlohmann::json::array_t& segment2_insertions,
   const nlohmann::json::array_t& gene1_insertions
) {
   return {
      {"primaryKey", primary_key},
      {"segment1", {{"sequence", "AAAA"}, {"insertions", segment1_insertions}}},
      {"segment2", {{"sequence", "GG"}, {"insertions", segment2_insertions}}},
      {"gene1", {{"sequence", "MK"}, {"insertions", gene1_insertions}}},
   };
}

const auto DATABASE_CONFIG = R"(
schema:
  instanceName: "test"
  metadata:
    - name: "primaryKey"
      type: "string"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES =
   ReferenceGenomes{{{"segment1", "AAAA"}, {"segment2", "GG"}}, {{"gene1", "MK"}}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {createData("s1", {"2:TT"}, {}, {}),
       createData("s2", {"2:TT"}, {}, {}),
       createData("s3", {"3:G"}, {"1:CC"}, {}),
       createData("s4", {}, {"1:CC"}, {"1:W"}),
       createData("s5", {"2:TT"}, {}, {"1:W"}),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

// ---- nucleotide insertions() ----

const QueryTestScenario INSERTIONS_ALL_FIELDS = {
   .name = "INSERTIONS_ALL_FIELDS",
   .query = "default.insertions().orderBy({sequenceName, position})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":2,"insertedSymbols":"TT","sequenceName":"segment1","count":3},
      {"position":3,"insertedSymbols":"G","sequenceName":"segment1","count":1},
      {"position":1,"insertedSymbols":"CC","sequenceName":"segment2","count":2}
   ])"),
};

const QueryTestScenario INSERTIONS_SEQUENCE_NAMES_SELECTS = {
   .name = "INSERTIONS_SEQUENCE_NAMES_SELECTS",
   .query = "default.insertions(sequenceNames:={segment1}).orderBy({position})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":2,"insertedSymbols":"TT","sequenceName":"segment1","count":3},
      {"position":3,"insertedSymbols":"G","sequenceName":"segment1","count":1}
   ])"),
};

const QueryTestScenario INSERTIONS_WITH_INPUT_FILTER = {
   .name = "INSERTIONS_WITH_INPUT_FILTER",
   .query =
      "default.filter(primaryKey = 's1' || primaryKey = 's2')"
      ".insertions().orderBy({sequenceName, position})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":2,"insertedSymbols":"TT","sequenceName":"segment1","count":2}
   ])"),
};

const QueryTestScenario INSERTIONS_ON_NON_SCAN = {
   .name = "INSERTIONS_ON_NON_SCAN",
   .query = "default.project({primaryKey}).insertions()",
   .expected_error_message = "insertions() must be applied to a table scan",
};

const QueryTestScenario INSERTIONS_UNKNOWN_SEQUENCE_NAME = {
   .name = "INSERTIONS_UNKNOWN_SEQUENCE_NAME",
   .query = "default.insertions(sequenceNames:={unknownSegment})",
   .expected_error_message =
      "The database does not contain the Nucleotide sequence 'unknownSegment'",
};

// gene1 exists but is an amino acid sequence
const QueryTestScenario INSERTIONS_WRONG_TYPE_SEQUENCE_NAME = {
   .name = "INSERTIONS_WRONG_TYPE_SEQUENCE_NAME",
   .query = "default.insertions(sequenceNames:={gene1})",
   .expected_error_message = "The database does not contain the Nucleotide sequence 'gene1'",
};

// ---- amino acid aminoAcidInsertions() ----

const QueryTestScenario AA_INSERTIONS_ALL_FIELDS = {
   .name = "AA_INSERTIONS_ALL_FIELDS",
   .query = "default.aminoAcidInsertions()",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":1,"insertedSymbols":"W","sequenceName":"gene1","count":2}
   ])"),
};

const QueryTestScenario AA_INSERTIONS_SEQUENCE_NAMES_SELECTS = {
   .name = "AA_INSERTIONS_SEQUENCE_NAMES_SELECTS",
   .query = "default.aminoAcidInsertions(sequenceNames:={gene1})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":1,"insertedSymbols":"W","sequenceName":"gene1","count":2}
   ])"),
};

// segment1 is a nucleotide sequence
const QueryTestScenario AA_INSERTIONS_WRONG_TYPE_SEQUENCE_NAME = {
   .name = "AA_INSERTIONS_WRONG_TYPE_SEQUENCE_NAME",
   .query = "default.aminoAcidInsertions(sequenceNames:={segment1})",
   .expected_error_message = "The database does not contain the AminoAcid sequence 'segment1'",
};

}  // namespace

QUERY_TEST(
   InsertionsNode,
   TEST_DATA,
   ::testing::Values(
      INSERTIONS_ALL_FIELDS,
      INSERTIONS_SEQUENCE_NAMES_SELECTS,
      INSERTIONS_WITH_INPUT_FILTER,
      INSERTIONS_ON_NON_SCAN,
      INSERTIONS_UNKNOWN_SEQUENCE_NAME,
      INSERTIONS_WRONG_TYPE_SEQUENCE_NAME
   )
);

QUERY_TEST(
   InsertionsNodeAminoAcid,
   TEST_DATA,
   ::testing::Values(
      AA_INSERTIONS_ALL_FIELDS,
      AA_INSERTIONS_SEQUENCE_NAMES_SELECTS,
      AA_INSERTIONS_WRONG_TYPE_SEQUENCE_NAME
   )
);
