#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/storage/column/column.h"
#include "rhydb/test/large_dataset.test.h"
#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::buildLargeTestDataset;
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
      {
         createData("s1", {"2:TT"}, {}, {}),
         createData("s2", {"2:TT"}, {}, {}),
         createData("s3", {"3:G"}, {"1:CC"}, {}),
         createData("s4", {}, {"1:CC"}, {"1:W"}),
         createData("s5", {"2:TT"}, {}, {"1:W"}),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

// ---- nucleotide insertions() ----

const QueryTestScenario INSERTIONS_ALL_FIELDS = {
   .name = "INSERTIONS_ALL_FIELDS",
   .query = "data.insertions().order(by:={sequenceName, position})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":2,"insertedSymbols":"TT","sequenceName":"segment1","count":3},
      {"position":3,"insertedSymbols":"G","sequenceName":"segment1","count":1},
      {"position":1,"insertedSymbols":"CC","sequenceName":"segment2","count":2}
   ])"),
};

const QueryTestScenario INSERTIONS_SEQUENCE_NAMES_SELECTS = {
   .name = "INSERTIONS_SEQUENCE_NAMES_SELECTS",
   .query = "data.insertions(sequenceNames:={segment1}).order(by:={position})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":2,"insertedSymbols":"TT","sequenceName":"segment1","count":3},
      {"position":3,"insertedSymbols":"G","sequenceName":"segment1","count":1}
   ])"),
};

const QueryTestScenario INSERTIONS_WITH_INPUT_FILTER = {
   .name = "INSERTIONS_WITH_INPUT_FILTER",
   .query =
      "data.filter(primaryKey = 's1' || primaryKey = 's2')"
      ".insertions().order(by:={sequenceName, position})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":2,"insertedSymbols":"TT","sequenceName":"segment1","count":2}
   ])"),
};

const QueryTestScenario INSERTIONS_ON_NON_SCAN = {
   .name = "INSERTIONS_ON_NON_SCAN",
   .query = "data.schema().insertions()",
   .expected_error_message = "insertions() must be applied to a table scan",
};

const QueryTestScenario INSERTIONS_UNKNOWN_SEQUENCE_NAME = {
   .name = "INSERTIONS_UNKNOWN_SEQUENCE_NAME",
   .query = "data.insertions(sequenceNames:={unknownSegment})",
   .expected_error_message =
      "The database does not contain the Nucleotide sequence 'unknownSegment'",
};

// gene1 exists but is an amino acid sequence
const QueryTestScenario INSERTIONS_WRONG_TYPE_SEQUENCE_NAME = {
   .name = "INSERTIONS_WRONG_TYPE_SEQUENCE_NAME",
   .query = "data.insertions(sequenceNames:={gene1})",
   .expected_error_message = "The database does not contain the Nucleotide sequence 'gene1'",
};

// ---- amino acid aminoAcidInsertions() ----

const QueryTestScenario AA_INSERTIONS_ALL_FIELDS = {
   .name = "AA_INSERTIONS_ALL_FIELDS",
   .query = "data.aminoAcidInsertions()",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":1,"insertedSymbols":"W","sequenceName":"gene1","count":2}
   ])"),
};

const QueryTestScenario AA_INSERTIONS_SEQUENCE_NAMES_SELECTS = {
   .name = "AA_INSERTIONS_SEQUENCE_NAMES_SELECTS",
   .query = "data.aminoAcidInsertions(sequenceNames:={gene1})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"position":1,"insertedSymbols":"W","sequenceName":"gene1","count":2}
   ])"),
};

// segment1 is a nucleotide sequence
const QueryTestScenario AA_INSERTIONS_WRONG_TYPE_SEQUENCE_NAME = {
   .name = "AA_INSERTIONS_WRONG_TYPE_SEQUENCE_NAME",
   .query = "data.aminoAcidInsertions(sequenceNames:={segment1})",
   .expected_error_message = "The database does not contain the AminoAcid sequence 'segment1'",
};

// ---------------------------------------------------------------------------
// Cross-chunk insertions
// ---------------------------------------------------------------------------

// Two full chunks and a partially filled third one
constexpr size_t CROSS_CHUNK_SIZE = rhydb::storage::column::COLUMN_CHUNK_SIZE;
constexpr size_t CROSS_CHUNK_NUM_ROWS = (2 * CROSS_CHUNK_SIZE) + 10'000;

const auto CROSS_CHUNK_DATABASE_CONFIG = R"(
schema:
  instanceName: "large dataset"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "rowIndex"
      type: "int"
  primaryKey: "primaryKey"
)";

const auto CROSS_CHUNK_REFERENCE_GENOMES =
   ReferenceGenomes{{{"segment1", "AAAA"}}, {{"gene1", "MK"}}};

// segment1 has the insertion 2:TT in every even row, gene1 has 1:W in every third row
nlohmann::json crossChunkRow(const size_t row_index) {
   const nlohmann::json::array_t nucleotide_insertions =
      row_index % 2 == 0 ? nlohmann::json::array_t{"2:TT"} : nlohmann::json::array_t{};
   const nlohmann::json::array_t amino_acid_insertions =
      row_index % 3 == 0 ? nlohmann::json::array_t{"1:W"} : nlohmann::json::array_t{};
   return {
      {"primaryKey", "id_" + std::to_string(row_index)},
      {"rowIndex", row_index},
      {"segment1", {{"sequence", "AAAA"}, {"insertions", nucleotide_insertions}}},
      {"gene1", {{"sequence", "MK"}, {"insertions", amino_acid_insertions}}},
   };
}

const QueryTestData CROSS_CHUNK_TEST_DATA{
   .ndjson_input_data = buildLargeTestDataset(crossChunkRow, CROSS_CHUNK_NUM_ROWS),
   .database_config = CROSS_CHUNK_DATABASE_CONFIG,
   .reference_genomes = CROSS_CHUNK_REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

// Every chunk passes the filter in full, including the partially filled last chunk
const QueryTestScenario INSERTIONS_ACROSS_CHUNKS_UNFILTERED = {
   .name = "INSERTIONS_ACROSS_CHUNKS_UNFILTERED",
   .query = "data.insertions()",
   .expected_query_result = nlohmann::json::array({
      {{"position", 2}, {"insertedSymbols", "TT"}, {"sequenceName", "segment1"}, {"count", 70'536}},
   }),
};

// Chunk 0 passes in full, chunk 1 only for its first 1000 rows, chunk 2 not at all
const QueryTestScenario INSERTIONS_ACROSS_CHUNKS_FULL_THEN_PARTIAL = {
   .name = "INSERTIONS_ACROSS_CHUNKS_FULL_THEN_PARTIAL",
   .query = "data.filter(rowIndex < 66536).insertions()",
   .expected_query_result = nlohmann::json::array({
      {{"position", 2}, {"insertedSymbols", "TT"}, {"sequenceName", "segment1"}, {"count", 33'268}},
   }),
};

// Chunk 0 passes only for its first 100 rows, chunk 1 not at all, chunk 2 in full
const QueryTestScenario INSERTIONS_ACROSS_CHUNKS_PARTIAL_THEN_FULL = {
   .name = "INSERTIONS_ACROSS_CHUNKS_PARTIAL_THEN_FULL",
   .query = "data.filter(rowIndex < 100 || rowIndex >= 131072).insertions()",
   .expected_query_result = nlohmann::json::array({
      {{"position", 2}, {"insertedSymbols", "TT"}, {"sequenceName", "segment1"}, {"count", 5'050}},
   }),
};

const QueryTestScenario AA_INSERTIONS_ACROSS_CHUNKS_UNFILTERED = {
   .name = "AA_INSERTIONS_ACROSS_CHUNKS_UNFILTERED",
   .query = "data.aminoAcidInsertions()",
   .expected_query_result = nlohmann::json::array({
      {{"position", 1}, {"insertedSymbols", "W"}, {"sequenceName", "gene1"}, {"count", 47'024}},
   }),
};

const QueryTestScenario AA_INSERTIONS_ACROSS_CHUNKS_FULL_THEN_PARTIAL = {
   .name = "AA_INSERTIONS_ACROSS_CHUNKS_FULL_THEN_PARTIAL",
   .query = "data.filter(rowIndex < 66536).aminoAcidInsertions()",
   .expected_query_result = nlohmann::json::array({
      {{"position", 1}, {"insertedSymbols", "W"}, {"sequenceName", "gene1"}, {"count", 22'179}},
   }),
};

const QueryTestScenario AA_INSERTIONS_ACROSS_CHUNKS_PARTIAL_THEN_FULL = {
   .name = "AA_INSERTIONS_ACROSS_CHUNKS_PARTIAL_THEN_FULL",
   .query = "data.filter(rowIndex < 100 || rowIndex >= 131072).aminoAcidInsertions()",
   .expected_query_result = nlohmann::json::array({
      {{"position", 1}, {"insertedSymbols", "W"}, {"sequenceName", "gene1"}, {"count", 3'367}},
   }),
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

QUERY_TEST(
   InsertionsNodeCrossChunk,
   CROSS_CHUNK_TEST_DATA,
   ::testing::Values(
      INSERTIONS_ACROSS_CHUNKS_UNFILTERED,
      INSERTIONS_ACROSS_CHUNKS_FULL_THEN_PARTIAL,
      INSERTIONS_ACROSS_CHUNKS_PARTIAL_THEN_FULL,
      AA_INSERTIONS_ACROSS_CHUNKS_UNFILTERED,
      AA_INSERTIONS_ACROSS_CHUNKS_FULL_THEN_PARTIAL,
      AA_INSERTIONS_ACROSS_CHUNKS_PARTIAL_THEN_FULL
   )
);
