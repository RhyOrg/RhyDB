#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

using boost::uuids::random_generator;

nlohmann::json createDataWithSequences(
   const std::string& nucleotideSequence,
   const std::string& aminoAcidSequence
) {
   random_generator generator;
   const auto primary_key = generator();
   return {
      {"primaryKey", "id_" + to_string(primary_key)},
      {"unaligned_segment1", {}},
      {"segment1", {{"sequence", nucleotideSequence}, {"insertions", nlohmann::json::array()}}},
      {"gene1", {{"sequence", aminoAcidSequence}, {"insertions", nlohmann::json::array()}}},
   };
}

const nlohmann::json DATA_SAME_AS_REFERENCE = createDataWithSequences("ATGCN", "M*");
const nlohmann::json DATA_SAME_AS_REFERENCE2 = createDataWithSequences("ATGCN", "C*");
const nlohmann::json DATA_WITH_ALL_N = createDataWithSequences("NNNNN", "M*");
const nlohmann::json DATA_WITH_ALL_MUTATED = createDataWithSequences("CATTT", "X*");

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
   {{"segment1", "ATGCN"}},
   {{"gene1", "M*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {DATA_SAME_AS_REFERENCE, DATA_SAME_AS_REFERENCE2, DATA_WITH_ALL_N, DATA_WITH_ALL_MUTATED},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario HAS_NUCLEOTIDE_MUTATION = {
   .name = "HAS_NUCLEOTIDE_MUTATION",
   .query =
      "default.filter(hasMutation(position:=1, "
      "sequenceName:='segment1')).groupBy({count:=count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 1}])"),
};

const QueryTestScenario HAS_AMINO_ACID_MUTATION = {
   .name = "HAS_AMINO_ACID_MUTATION",
   .query =
      "default.filter(hasAAMutation(position:=1, sequenceName:='gene1')).groupBy({count:=count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 1}])"),
};

const QueryTestScenario HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE = {
   .name = "HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE",
   .query =
      "default.filter(hasMutation(position:=2000, "
      "sequenceName:='segment1')).groupBy({count:=count()})",
   .expected_error_message = "HasNucleotideMutation position is out of bounds 2000 > 5",
};

const QueryTestScenario HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE_EDGE_LOW = {
   .name = "HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE_EDGE_LOW",
   .query =
      "default.filter(hasMutation(position:=0, "
      "sequenceName:='segment1')).groupBy({count:=count()})",
   .expected_error_message = "The field 'position' is 1-indexed. Value of 0 not allowed.",
};

const QueryTestScenario HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE_EDGE_HIGH = {
   .name = "HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE_EDGE_HIGH",
   .query =
      "default.filter(hasMutation(position:=6, "
      "sequenceName:='segment1')).groupBy({count:=count()})",
   .expected_error_message = "HasNucleotideMutation position is out of bounds 6 > 5",
};

const QueryTestScenario HAS_NUCLEOTIDE_MUTATION_IN_RANGE_EDGE = {
   .name = "HAS_NUCLEOTIDE_MUTATION_IN_RANGE_EDGE",
   .query =
      "default.filter(hasMutation(position:=5, "
      "sequenceName:='segment1')).groupBy({count:=count()})",
   .expected_query_result = nlohmann::json::parse(R"([{"count": 1}])"),
};

const QueryTestScenario HAS_AMINO_ACID_MUTATION_OUT_OF_RANGE = {
   .name = "HAS_AMINO_ACID_MUTATION_OUT_OF_RANGE",
   .query =
      "default.filter(hasAAMutation(position:=1000, "
      "sequenceName:='gene1')).groupBy({count:=count()})",
   .expected_error_message = "HasAminoAcidMutation position is out of bounds 1000 > 2",
};

// A sequence name is always required; there is no implicit default even though the reference genome
// has only a single nucleotide sequence.
const QueryTestScenario HAS_NUCLEOTIDE_MUTATION_WITHOUT_SEQUENCE_NAME = {
   .name = "HAS_NUCLEOTIDE_MUTATION_WITHOUT_SEQUENCE_NAME",
   .query = "default.filter(hasMutation(position:=1)).groupBy({count:=count()})",
   .expected_error_message = "hasMutation() requires argument 'sequenceName'",
};

// Amino acid filters always require a sequence name.
const QueryTestScenario HAS_AMINO_ACID_MUTATION_WITHOUT_SEQUENCE_NAME = {
   .name = "HAS_AMINO_ACID_MUTATION_WITHOUT_SEQUENCE_NAME",
   .query = "default.filter(hasAAMutation(position:=1)).groupBy({count:=count()})",
   .expected_error_message = "hasAAMutation() requires argument 'sequenceName'",
};

}  // namespace

QUERY_TEST(
   HasMutation,
   TEST_DATA,
   ::testing::Values(
      HAS_NUCLEOTIDE_MUTATION,
      HAS_AMINO_ACID_MUTATION,
      HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE,
      HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE_EDGE_LOW,
      HAS_NUCLEOTIDE_MUTATION_OUT_OF_RANGE_EDGE_HIGH,
      HAS_NUCLEOTIDE_MUTATION_IN_RANGE_EDGE,
      HAS_AMINO_ACID_MUTATION_OUT_OF_RANGE,
      HAS_NUCLEOTIDE_MUTATION_WITHOUT_SEQUENCE_NAME,
      HAS_AMINO_ACID_MUTATION_WITHOUT_SEQUENCE_NAME
   )
);
