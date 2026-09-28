#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

const std::vector<nlohmann::json> DATA = {
   {{"primaryKey", "id_0"},
    {"segment1", nullptr},
    {"segment2", nullptr},
    {"gene1", nullptr},
    {"unaligned_segment1", nullptr},
    {"unaligned_segment2", nullptr},},
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ACGT"}, {"segment2", "TTTT"}},
   {{"gene1", "MYK*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario ALL_REFERENCE_GENOMES = {
   .name = "ALL_REFERENCE_GENOMES",
   .query = "reference_genomes.orderBy({name})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"name": "gene1", "type": "amino_acid", "sequence": "MYK*"},
      {"name": "segment1", "type": "nucleotide", "sequence": "ACGT"},
      {"name": "segment2", "type": "nucleotide", "sequence": "TTTT"}
   ])"),
};

const QueryTestScenario FILTER_BY_TYPE = {
   .name = "FILTER_BY_TYPE",
   .query = "reference_genomes.filter(type = 'nucleotide').project({name}).orderBy({name})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"name": "segment1"},
      {"name": "segment2"}
   ])"),
};

}  // namespace

QUERY_TEST(
   ReferenceGenomesTable,
   TEST_DATA,
   ::testing::Values(ALL_REFERENCE_GENOMES, FILTER_BY_TYPE)
);
