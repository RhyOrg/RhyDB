#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const std::string& segment1,
   const std::string& segment2,
   const std::string& gene1
) {
   return {
      {"primaryKey", primary_key},
      {"segment1", {{"sequence", segment1}, {"insertions", nlohmann::json::array()}}},
      {"segment2", {{"sequence", segment2}, {"insertions", nlohmann::json::array()}}},
      {"gene1", {{"sequence", gene1}, {"insertions", nlohmann::json::array()}}},
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
   ReferenceGenomes{{{"segment1", "ATGC"}, {"segment2", "GG"}}, {{"gene1", "MK"}}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("s1", "ATGC", "GG", "MK"),  // all references
         createData("s2", "CTGC", "GG", "TK"),  // segment1 A->C, gene1 M->T
         createData("s3", "CTGC", "TG", "TK"),  // segment1 A->C, segment2 G->T, gene1 M->T
         createData("s4", "GTGC", "GG", "MK"),  // segment1 A->G
         createData("s5", "NTGC", "GG", "MK"),   // segment1 position 1 is N (uncovered)
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

// ---- nucleotide mutations() ----

const QueryTestScenario MUTATIONS_ALL_FIELDS = {
   .name = "MUTATIONS_ALL_FIELDS",
   .query = "default.mutations(minProportion:=0.0).orderBy({sequenceName, position, mutationTo})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"A","mutationTo":"C","sequenceName":"segment1","position":1,
       "proportion":0.5,"coverage":4,"count":2},
      {"mutationFrom":"A","mutationTo":"G","sequenceName":"segment1","position":1,
       "proportion":0.25,"coverage":4,"count":1},
      {"mutationFrom":"G","mutationTo":"T","sequenceName":"segment2","position":1,
       "proportion":0.2,"coverage":5,"count":1}
   ])"),
};

const QueryTestScenario MUTATIONS_MIN_PROPORTION_KEEPS_SUBSET = {
   .name = "MUTATIONS_MIN_PROPORTION_KEEPS_SUBSET",
   .query = "default.mutations(minProportion:=0.3)",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"A","mutationTo":"C","sequenceName":"segment1","position":1,
       "proportion":0.5,"coverage":4,"count":2}
   ])"),
};

const QueryTestScenario MUTATIONS_MIN_PROPORTION_EXCLUDES_ALL = {
   .name = "MUTATIONS_MIN_PROPORTION_EXCLUDES_ALL",
   .query = "default.mutations(minProportion:=0.6)",
   .expected_query_result = nlohmann::json::array(),
};

// Regression for #1530: orderBy on an empty mutations result must return empty, not crash.
const QueryTestScenario MUTATIONS_EMPTY_ORDER_BY_PROPORTION = {
   .name = "MUTATIONS_EMPTY_ORDER_BY_PROPORTION",
   .query = "default.mutations(minProportion:=0.6).orderBy({proportion})",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario MUTATIONS_EMPTY_ORDER_BY_PROPORTION_WITH_LIMIT = {
   .name = "MUTATIONS_EMPTY_ORDER_BY_PROPORTION_WITH_LIMIT",
   .query = "default.mutations(minProportion:=0.6).orderBy({proportion}).limit(5)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario MUTATIONS_SEQUENCE_NAMES_SELECTS = {
   .name = "MUTATIONS_SEQUENCE_NAMES_SELECTS",
   .query =
      "default.mutations(minProportion:=0.0, sequenceNames:={segment1})"
      ".orderBy({position, mutationTo})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"A","mutationTo":"C","sequenceName":"segment1","position":1,
       "proportion":0.5,"coverage":4,"count":2},
      {"mutationFrom":"A","mutationTo":"G","sequenceName":"segment1","position":1,
       "proportion":0.25,"coverage":4,"count":1}
   ])"),
};

const QueryTestScenario MUTATIONS_WITH_INPUT_FILTER = {
   .name = "MUTATIONS_WITH_INPUT_FILTER",
   .query =
      "default.filter(primaryKey = 's2' || primaryKey = 's3')"
      ".mutations(minProportion:=0.0).orderBy({sequenceName, position, mutationTo})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"A","mutationTo":"C","sequenceName":"segment1","position":1,
       "proportion":1.0,"coverage":2,"count":2},
      {"mutationFrom":"G","mutationTo":"T","sequenceName":"segment2","position":1,
       "proportion":0.5,"coverage":2,"count":1}
   ])"),
};

const QueryTestScenario MUTATIONS_FIELDS_NARROWED = {
   .name = "MUTATIONS_FIELDS_NARROWED",
   .query =
      "default.mutations(minProportion:=0.0, sequenceNames:={segment2}, fields:={position, count})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":1,"count":1}
   ])"),
};

const QueryTestScenario MUTATIONS_INVALID_MIN_PROPORTION = {
   .name = "MUTATIONS_INVALID_MIN_PROPORTION",
   .query = "default.mutations(minProportion:=1.5)",
   .expected_error_message = "Invalid proportion: minProportion must be in interval [0.0, 1.0]",
};

const QueryTestScenario MUTATIONS_MISSING_MIN_PROPORTION = {
   .name = "MUTATIONS_MISSING_MIN_PROPORTION",
   .query = "default.mutations()",
   .expected_error_message = "mutations() requires argument 'minProportion'",
};

const QueryTestScenario MUTATIONS_ON_NON_SCAN = {
   .name = "MUTATIONS_ON_NON_SCAN",
   .query = "default.project({primaryKey}).mutations(minProportion:=0.1)",
   .expected_error_message = "mutations() must be applied to a table scan",
};

const QueryTestScenario MUTATIONS_UNKNOWN_SEQUENCE_NAME = {
   .name = "MUTATIONS_UNKNOWN_SEQUENCE_NAME",
   .query = "default.mutations(minProportion:=0.1, sequenceNames:={unknownSegment})",
   .expected_error_message =
      "The database does not contain the Nucleotide sequence 'unknownSegment'",
};

// gene1 exists but is an amino acid sequence
const QueryTestScenario MUTATIONS_WRONG_TYPE_SEQUENCE_NAME = {
   .name = "MUTATIONS_WRONG_TYPE_SEQUENCE_NAME",
   .query = "default.mutations(minProportion:=0.1, sequenceNames:={gene1})",
   .expected_error_message = "The database does not contain the Nucleotide sequence 'gene1'",
};

const QueryTestScenario MUTATIONS_INVALID_FIELD = {
   .name = "MUTATIONS_INVALID_FIELD",
   .query = "default.mutations(minProportion:=0.1, fields:={notAField})",
   .expected_error_message =
      "The attribute 'fields' contains an invalid field 'notAField'. Valid fields are "
      "mutationFrom, mutationTo, position, sequenceName, proportion, coverage, count.",
};

// ---- filtering the OUTPUT of mutations() ----

const QueryTestScenario FILTER_MUTATIONS_OUTPUT_PROPORTION_KEEP = {
   .name = "FILTER_MUTATIONS_OUTPUT_PROPORTION_KEEP",
   .query = "default.mutations(minProportion:=0.0).filter(proportion > 0.4)",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"A","mutationTo":"C","sequenceName":"segment1","position":1,
       "proportion":0.5,"coverage":4,"count":2}
   ])"),
};

const QueryTestScenario FILTER_MUTATIONS_OUTPUT_PROPORTION_DROP = {
   .name = "FILTER_MUTATIONS_OUTPUT_PROPORTION_DROP",
   .query = "default.mutations(minProportion:=0.0).filter(proportion > 0.6)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario FILTER_MUTATIONS_OUTPUT_COUNT_AND_SEQUENCE = {
   .name = "FILTER_MUTATIONS_OUTPUT_COUNT_AND_SEQUENCE",
   .query = "default.mutations(minProportion:=0.0).filter(count = 1 && sequenceName = 'segment2')",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"G","mutationTo":"T","sequenceName":"segment2","position":1,
       "proportion":0.2,"coverage":5,"count":1}
   ])"),
};

// ---- amino acid aminoAcidMutations() ----

const QueryTestScenario AA_MUTATIONS_ALL_FIELDS = {
   .name = "AA_MUTATIONS_ALL_FIELDS",
   .query = "default.aminoAcidMutations(minProportion:=0.0)",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"M","mutationTo":"T","sequenceName":"gene1","position":1,
       "proportion":0.4,"coverage":5,"count":2}
   ])"),
};

const QueryTestScenario AA_MUTATIONS_MIN_PROPORTION_EXCLUDES = {
   .name = "AA_MUTATIONS_MIN_PROPORTION_EXCLUDES",
   .query = "default.aminoAcidMutations(minProportion:=0.5)",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario AA_MUTATIONS_SEQUENCE_NAMES_SELECTS = {
   .name = "AA_MUTATIONS_SEQUENCE_NAMES_SELECTS",
   .query = "default.aminoAcidMutations(minProportion:=0.0, sequenceNames:={gene1})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"mutationFrom":"M","mutationTo":"T","sequenceName":"gene1","position":1,
       "proportion":0.4,"coverage":5,"count":2}
   ])"),
};

// segment1 is a nucleotide sequence
const QueryTestScenario AA_MUTATIONS_WRONG_TYPE_SEQUENCE_NAME = {
   .name = "AA_MUTATIONS_WRONG_TYPE_SEQUENCE_NAME",
   .query = "default.aminoAcidMutations(minProportion:=0.1, sequenceNames:={segment1})",
   .expected_error_message = "The database does not contain the AminoAcid sequence 'segment1'",
};

}  // namespace

QUERY_TEST(
   MutationsNode,
   TEST_DATA,
   ::testing::Values(
      MUTATIONS_ALL_FIELDS,
      MUTATIONS_MIN_PROPORTION_KEEPS_SUBSET,
      MUTATIONS_MIN_PROPORTION_EXCLUDES_ALL,
      MUTATIONS_EMPTY_ORDER_BY_PROPORTION,
      MUTATIONS_EMPTY_ORDER_BY_PROPORTION_WITH_LIMIT,
      MUTATIONS_SEQUENCE_NAMES_SELECTS,
      MUTATIONS_WITH_INPUT_FILTER,
      MUTATIONS_FIELDS_NARROWED,
      MUTATIONS_INVALID_MIN_PROPORTION,
      MUTATIONS_MISSING_MIN_PROPORTION,
      MUTATIONS_ON_NON_SCAN,
      MUTATIONS_UNKNOWN_SEQUENCE_NAME,
      MUTATIONS_WRONG_TYPE_SEQUENCE_NAME,
      MUTATIONS_INVALID_FIELD
   )
);

QUERY_TEST(
   MutationsOutputFilter,
   TEST_DATA,
   ::testing::Values(
      FILTER_MUTATIONS_OUTPUT_PROPORTION_KEEP,
      FILTER_MUTATIONS_OUTPUT_PROPORTION_DROP,
      FILTER_MUTATIONS_OUTPUT_COUNT_AND_SEQUENCE
   )
);

QUERY_TEST(
   MutationsNodeAminoAcid,
   TEST_DATA,
   ::testing::Values(
      AA_MUTATIONS_ALL_FIELDS,
      AA_MUTATIONS_MIN_PROPORTION_EXCLUDES,
      AA_MUTATIONS_SEQUENCE_NAMES_SELECTS,
      AA_MUTATIONS_WRONG_TYPE_SEQUENCE_NAME
   )
);
