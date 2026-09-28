#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

namespace {
nlohmann::json createDataWithUnalignedSequences(
   const std::string& primaryKey,
   const std::string& date,
   const nlohmann::json& segment1,
   const nlohmann::json& segment2
) {
   return {
      {"primaryKey", primaryKey},
      {"date", date},
      {"segment1", nullptr},
      {"segment2", nullptr},
      {"unaligned_segment1", segment1},
      {"unaligned_segment2", segment2},
      {"gene1", nullptr},
      {"gene2", nullptr},
   };
}

const std::vector<nlohmann::json> DATA = {
   createDataWithUnalignedSequences("bothSegments", "2024-08-01", "A", "G"),
   createDataWithUnalignedSequences("onlySegment1", "2024-08-03", "T", nullptr),
   createDataWithUnalignedSequences("onlySegment2", "2024-08-02", nullptr, "T"),
   createDataWithUnalignedSequences("noSegment", "2024-08-08", nullptr, nullptr),
   createDataWithUnalignedSequences("1", "2024-08-05", nullptr, "A"),
   createDataWithUnalignedSequences("2", "2024-08-03", nullptr, nullptr),
   createDataWithUnalignedSequences("3", "2024-08-02", nullptr, "AA"),
};

const auto DATABASE_CONFIG =
   R"(
 schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "date"
      type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "A"}, {"segment2", "T"}},
   {{"gene1", "*"}, {"gene2", "*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

std::string createFastaQuery(const std::string& primaryKey) {
   return fmt::format(
      "default.filter(primaryKey = '{}').project({{primaryKey, unaligned_segment1, "
      "unaligned_segment2}})",
      primaryKey
   );
}

const QueryTestScenario SEQUENCE_WITH_BOTH_SEGMENTS_SCENARIO = {
   .name = "SEQUENCE_WITH_BOTH_SEGMENTS_SCENARIO",
   .query = createFastaQuery("bothSegments"),
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "bothSegments"}, {"unaligned_segment1", "A"}, {"unaligned_segment2", "G"}}}
   ),
};

const QueryTestScenario SEQUENCE_WITH_ONLY_FIRST_SEGMENT_SCENARIO = {
   .name = "SEQUENCE_WITH_ONLY_FIRST_SEGMENT_SCENARIO",
   .query = createFastaQuery("onlySegment1"),
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "onlySegment1"}, {"unaligned_segment1", "T"}, {"unaligned_segment2", nullptr},
      },}
   ),
};

const QueryTestScenario SEQUENCE_WITH_ONLY_SECOND_SEGMENT_SCENARIO = {
   .name = "SEQUENCE_WITH_ONLY_SECOND_SEGMENT_SCENARIO",
   .query = createFastaQuery("onlySegment2"),
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "onlySegment2"}, {"unaligned_segment1", nullptr}, {"unaligned_segment2", "T"},
      },}
   ),
};

const QueryTestScenario SEQUENCE_WITH_NO_SEGMENT_SCENARIO = {
   .name = "SEQUENCE_WITH_NO_SEGMENT_SCENARIO",
   .query = createFastaQuery("noSegment"),
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "noSegment"},
        {"unaligned_segment1", nullptr},
        {"unaligned_segment2", nullptr},},}
   ),
};

const QueryTestScenario DOWNLOAD_ALL_SEQUENCES_SCENARIO = {
   .name = "DOWNLOAD_ALL_SEQUENCES_SCENARIO",
   .query =
      "default.project({primaryKey, unaligned_segment1, unaligned_segment2}).orderBy({primaryKey})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "1"}, {"unaligned_segment1", nullptr}, {"unaligned_segment2", "A"}},
       {{"primaryKey", "2"}, {"unaligned_segment1", nullptr}, {"unaligned_segment2", nullptr}},
       {{"primaryKey", "3"}, {"unaligned_segment1", nullptr}, {"unaligned_segment2", "AA"}},
       {{"primaryKey", "bothSegments"}, {"unaligned_segment1", "A"}, {"unaligned_segment2", "G"}},
       {{"primaryKey", "noSegment"},
        {"unaligned_segment1", nullptr},
        {"unaligned_segment2", nullptr},},
       {{"primaryKey", "onlySegment1"}, {"unaligned_segment1", "T"}, {"unaligned_segment2", nullptr},
       },
       {{"primaryKey", "onlySegment2"}, {"unaligned_segment1", nullptr}, {"unaligned_segment2", "T"},
       },}
   ),
   .query_options = rhydb::config::QueryOptions{.materialization_cutoff = 0},
};

const QueryTestScenario DOWNLOAD_ALL_DATA = {
   .name = "DOWNLOAD_ALL_DATA",
   .query =
      "default.project({primaryKey, unaligned_segment1, unaligned_segment2, date})"
      ".orderBy({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"date":"2024-08-05","primaryKey":"1","unaligned_segment1":null,"unaligned_segment2":"A"},
{"date":"2024-08-03","primaryKey":"2","unaligned_segment1":null,"unaligned_segment2":null},
{"date":"2024-08-02","primaryKey":"3","unaligned_segment1":null,"unaligned_segment2":"AA"},
{"date":"2024-08-01","primaryKey":"bothSegments","unaligned_segment1":"A","unaligned_segment2":"G"},
{"date":"2024-08-08","primaryKey":"noSegment","unaligned_segment1":null,"unaligned_segment2":null},
{"date":"2024-08-03","primaryKey":"onlySegment1","unaligned_segment1":"T","unaligned_segment2":null},
{"date":"2024-08-02","primaryKey":"onlySegment2","unaligned_segment1":null,"unaligned_segment2":"T"}])"
   ),
};

const QueryTestScenario DUPLICATE_FIELDS = {
   .name = "DUPLICATE_FIELDS",
   .query =
      "default.project({primaryKey, unaligned_segment1, unaligned_segment2, unaligned_segment1, "
      "date, date}).orderBy({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"date":"2024-08-05","primaryKey":"1","unaligned_segment1":null,"unaligned_segment2":"A"},
{"date":"2024-08-03","primaryKey":"2","unaligned_segment1":null,"unaligned_segment2":null},
{"date":"2024-08-02","primaryKey":"3","unaligned_segment1":null,"unaligned_segment2":"AA"},
{"date":"2024-08-01","primaryKey":"bothSegments","unaligned_segment1":"A","unaligned_segment2":"G"},
{"date":"2024-08-08","primaryKey":"noSegment","unaligned_segment1":null,"unaligned_segment2":null},
{"date":"2024-08-03","primaryKey":"onlySegment1","unaligned_segment1":"T","unaligned_segment2":null},
{"date":"2024-08-02","primaryKey":"onlySegment2","unaligned_segment1":null,"unaligned_segment2":"T"}])"
   ),
};

const QueryTestScenario ORDER_BY_NOT_IN_OUTPUT = {
   .name = "ORDER_BY_NOT_IN_OUTPUT",
   .query = "default.project({primaryKey, unaligned_segment1}).orderBy({date.desc()})",
   .expected_error_message =
      "OrderByField date is not contained in the result of this operation. "
      "Allowed values are primaryKey, unaligned_segment1.",
};

const QueryTestScenario ORDER_BY_ADDITIONAL_FIELD = {
   .name = "ORDER_BY_ADDITIONAL_FIELD",
   .query =
      "default.project({primaryKey, unaligned_segment1, unaligned_segment2, date})"
      ".orderBy({date.asc()})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"date":"2024-08-01","primaryKey":"bothSegments","unaligned_segment1":"A","unaligned_segment2":"G"},
{"date":"2024-08-02","primaryKey":"onlySegment2","unaligned_segment1":null,"unaligned_segment2":"T"},
{"date":"2024-08-02","primaryKey":"3","unaligned_segment1":null,"unaligned_segment2":"AA"},
{"date":"2024-08-03","primaryKey":"onlySegment1","unaligned_segment1":"T","unaligned_segment2":null},
{"date":"2024-08-03","primaryKey":"2","unaligned_segment1":null,"unaligned_segment2":null},
{"date":"2024-08-05","primaryKey":"1","unaligned_segment1":null,"unaligned_segment2":"A"},
{"date":"2024-08-08","primaryKey":"noSegment","unaligned_segment1":null,"unaligned_segment2":null}])"
   ),
};

}  // namespace

QUERY_TEST(
   FastaTest,
   TEST_DATA,
   ::testing::Values(
      SEQUENCE_WITH_BOTH_SEGMENTS_SCENARIO,
      SEQUENCE_WITH_ONLY_FIRST_SEGMENT_SCENARIO,
      SEQUENCE_WITH_ONLY_SECOND_SEGMENT_SCENARIO,
      SEQUENCE_WITH_NO_SEGMENT_SCENARIO,
      DOWNLOAD_ALL_SEQUENCES_SCENARIO,
      DOWNLOAD_ALL_DATA,
      ORDER_BY_NOT_IN_OUTPUT,
      ORDER_BY_ADDITIONAL_FIELD,
      DUPLICATE_FIELDS
   )
);
