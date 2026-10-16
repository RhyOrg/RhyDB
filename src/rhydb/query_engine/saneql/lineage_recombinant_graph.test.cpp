#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "rhydb/preprocessing/lineage_definition_file.h"
#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::preprocessing::LineageDefinitionFile;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

/* A double diamond with the recombinants L3 (of L2 and L0) and L4 (of the recombinant L3 and L5),
 * below ROOT, which no row carries:
 *
 *     ROOT
 *      |
 *      L1
 *     /  \
 *   L2    L0
 *    \   /  \
 *     L3     L5
 *       \   /
 *        L4
 */
const auto LINEAGE_TREE =
   LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
ROOT:
  parents: []
L1:
  parents: [ROOT]
L2:
  parents: [L1]
L0:
  parents: [L1]
L3:
  parents: [L2, L0]
L5:
  parents: [L0]
L4:
  parents: [L3, L5]
)"));

// The lineage of the row with primary key id_<i>.
const std::vector<std::string> ROW_LINEAGES =
   {"L1", "L3", "L1", "L2", "L0", "L4", "L5", "L4", "L5", "L5"};

std::vector<nlohmann::json> createData() {
   std::vector<nlohmann::json> data;
   data.reserve(ROW_LINEAGES.size());
   for (size_t row = 0; row < ROW_LINEAGES.size(); ++row) {
      data.push_back({
         {"primaryKey", fmt::format("id_{}", row)},
         {"pango_lineage", ROW_LINEAGES.at(row)},
         {"segment1", nullptr},
         {"unaligned_segment1", nullptr},
         {"gene1", nullptr},
      });
   }
   return data;
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "pango_lineage"
      type: "string"
      generateIndex: true
      generateLineageIndex: test_lineage_index
  primaryKey: "primaryKey"
)";

const QueryTestData TEST_DATA{
   .ndjson_input_data = createData(),
   .database_config = DATABASE_CONFIG,
   .reference_genomes = ReferenceGenomes{{{"segment1", "A"}}, {{"gene1", "*"}}},
   .lineage_trees = {{"test_lineage_index", LINEAGE_TREE}},
};

const std::vector<size_t> ALL_ROWS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

// For each queried lineage, the matching rows without sublineages and with sublineages in each
// recombinantFollowingMode.
struct ExpectedRows {
   std::vector<size_t> exact;
   std::vector<size_t> do_not_follow;
   std::vector<size_t> follow_if_fully_contained_in_clade;
   std::vector<size_t> always_follow;
};

const std::map<std::string, ExpectedRows> EXPECTED_ROWS = {
   {
      "ROOT",
      {
         .exact = {},
         .do_not_follow = {0, 2, 3, 4, 6, 8, 9},
         .follow_if_fully_contained_in_clade = ALL_ROWS,
         .always_follow = ALL_ROWS,
      },
   },
   {
      "L0",
      {
         .exact = {4},
         .do_not_follow = {4, 6, 8, 9},
         .follow_if_fully_contained_in_clade = {4, 6, 8, 9},
         .always_follow = {1, 4, 5, 6, 7, 8, 9},
      },
   },
   {
      "L1",
      {
         .exact = {0, 2},
         .do_not_follow = {0, 2, 3, 4, 6, 8, 9},
         .follow_if_fully_contained_in_clade = ALL_ROWS,
         .always_follow = ALL_ROWS,
      },
   },
   {
      "L2",
      {
         .exact = {3},
         .do_not_follow = {3},
         .follow_if_fully_contained_in_clade = {3},
         .always_follow = {1, 3, 5, 7},
      },
   },
   {
      "L3",
      {
         .exact = {1},
         .do_not_follow = {1},
         .follow_if_fully_contained_in_clade = {1},
         .always_follow = {1, 5, 7},
      },
   },
   {
      "L4",
      {
         .exact = {5, 7},
         .do_not_follow = {5, 7},
         .follow_if_fully_contained_in_clade = {5, 7},
         .always_follow = {5, 7},
      },
   },
   {
      "L5",
      {
         .exact = {6, 8, 9},
         .do_not_follow = {6, 8, 9},
         .follow_if_fully_contained_in_clade = {6, 8, 9},
         .always_follow = {5, 6, 7, 8, 9},
      },
   },
};

QueryTestScenario scenario(
   const std::string& name,
   const std::string& lineage,
   const std::string& arguments,
   const std::vector<size_t>& rows
) {
   nlohmann::json expected = nlohmann::json::array();
   for (const size_t row : rows) {
      expected.push_back({{"primaryKey", fmt::format("id_{}", row)}});
   }
   return {
      .name = fmt::format("{}_{}", lineage, name),
      .query = fmt::format(
         "data.filter(pango_lineage.lineage('{}'{})).project({{primaryKey}})", lineage, arguments
      ),
      .expected_query_result = expected,
   };
}

std::vector<QueryTestScenario> createScenarios() {
   std::vector<QueryTestScenario> scenarios;
   for (const auto& [lineage, expected] : EXPECTED_ROWS) {
      scenarios.push_back(scenario("EXACT", lineage, "", expected.exact));
      scenarios.push_back(scenario(
         "DO_NOT_FOLLOW",
         lineage,
         ", includeSublineages:=true, recombinantFollowingMode:='doNotFollow'",
         expected.do_not_follow
      ));
      scenarios.push_back(scenario(
         "FOLLOW_IF_FULLY_CONTAINED_IN_CLADE",
         lineage,
         ", includeSublineages:=true, recombinantFollowingMode:='followIfFullyContainedInClade'",
         expected.follow_if_fully_contained_in_clade
      ));
      scenarios.push_back(scenario(
         "ALWAYS_FOLLOW",
         lineage,
         ", includeSublineages:=true, recombinantFollowingMode:='alwaysFollow'",
         expected.always_follow
      ));
   }
   return scenarios;
}

}  // namespace

QUERY_TEST(LineageRecombinantGraphTest, TEST_DATA, ::testing::ValuesIn(createScenarios()))
