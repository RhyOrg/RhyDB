#include <nlohmann/json.hpp>

#include "rhydb/preprocessing/lineage_definition_file.h"
#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::preprocessing::LineageDefinitionFile;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createDataWithLineageValue(const std::string& primaryKey, nlohmann::json value) {
   return {
      {"primaryKey", primaryKey},
      {"pango_lineage", std::move(value)},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

// Rows carry canonical lineages as well as aliases. An alias matches exactly what its canonical
// lineage matches, whichever of the two is queried or stored.
const std::vector<nlohmann::json> DATA = {
   createDataWithLineageValue("id_0", "BASE.1"),
   createDataWithLineageValue("id_1", "B1"),
   createDataWithLineageValue("id_2", "CHILD"),
   createDataWithLineageValue("id_3", "C"),
   createDataWithLineageValue("id_4", "CHILD.2"),
   createDataWithLineageValue("id_5", "RECOMBINANT"),
   createDataWithLineageValue("id_6", "ISOLATED"),
   createDataWithLineageValue("id_7", "WITH'QUOTE"),
   createDataWithLineageValue("id_8", nullptr),
};

// With `lineageIndexType: table`, the lineage hierarchy is only available as the relation table
// `pango_lineage` and the alias table `pango_lineage_aliases`.
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
      lineageIndexType: table
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "A"}},
   {{"gene1", "*"}},
};

// ISOLATED has no parents and no children, so it is no endpoint of an edge of the hierarchy.
const auto LINEAGE_TREE =
   LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
BASE.1:
  aliases:
  - B1
  parents: []
CHILD:
  aliases:
  - C
  - C_OTHER
  parents:
  - BASE.1
CHILD.2:
  parents:
  - BASE.1
RECOMBINANT:
  parents:
  - CHILD
  - CHILD.2
ISOLATED:
  parents: []
"WITH'QUOTE":
  parents: []
)"));

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .lineage_trees = {{"test_lineage_index", LINEAGE_TREE}},
};

const QueryTestScenario CANONICAL_LINEAGE_MATCHES_ITS_ALIASES = {
   .name = "CANONICAL_LINEAGE_MATCHES_ITS_ALIASES",
   .query =
      "data.filter(pango_lineage.lineageFromTables('BASE.1', pango_lineage, "
      "pango_lineage_aliases)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"BASE.1","primaryKey":"id_0"},
{"pango_lineage":"B1","primaryKey":"id_1"}]
)"),
};

const QueryTestScenario ALIAS_MATCHES_ITS_CANONICAL_LINEAGE = {
   .name = "ALIAS_MATCHES_ITS_CANONICAL_LINEAGE",
   .query =
      "data.filter(pango_lineage.lineageFromTables('C_OTHER', pango_lineage, "
      "pango_lineage_aliases)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"CHILD","primaryKey":"id_2"},
{"pango_lineage":"C","primaryKey":"id_3"}]
)"),
};

const QueryTestScenario INCLUDING_SUBLINEAGES = {
   .name = "INCLUDING_SUBLINEAGES",
   .query =
      "data.filter(pango_lineage.lineageFromTables('BASE.1', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"BASE.1","primaryKey":"id_0"},
{"pango_lineage":"B1","primaryKey":"id_1"},
{"pango_lineage":"CHILD","primaryKey":"id_2"},
{"pango_lineage":"C","primaryKey":"id_3"},
{"pango_lineage":"CHILD.2","primaryKey":"id_4"}]
)"),
};

const QueryTestScenario ALIAS_INCLUDING_SUBLINEAGES = {
   .name = "ALIAS_INCLUDING_SUBLINEAGES",
   .query =
      "data.filter(pango_lineage.lineageFromTables('B1', pango_lineage, pango_lineage_aliases, "
      "includeSublineages:=true)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"BASE.1","primaryKey":"id_0"},
{"pango_lineage":"B1","primaryKey":"id_1"},
{"pango_lineage":"CHILD","primaryKey":"id_2"},
{"pango_lineage":"C","primaryKey":"id_3"},
{"pango_lineage":"CHILD.2","primaryKey":"id_4"}]
)"),
};

const QueryTestScenario ALWAYS_FOLLOW = {
   .name = "ALWAYS_FOLLOW",
   .query =
      "data.filter(pango_lineage.lineageFromTables('CHILD', pango_lineage, pango_lineage_aliases, "
      "includeSublineages:=true, recombinantFollowingMode:='alwaysFollow'))"
      ".project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"CHILD","primaryKey":"id_2"},
{"pango_lineage":"C","primaryKey":"id_3"},
{"pango_lineage":"RECOMBINANT","primaryKey":"id_5"}]
)"),
};

const QueryTestScenario FOLLOW_IF_FULLY_CONTAINED_IN_CLADE = {
   .name = "FOLLOW_IF_FULLY_CONTAINED_IN_CLADE",
   .query =
      "data.filter(pango_lineage.lineageFromTables('BASE.1', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true, "
      "recombinantFollowingMode:='followIfFullyContainedInClade'))"
      ".project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"BASE.1","primaryKey":"id_0"},
{"pango_lineage":"B1","primaryKey":"id_1"},
{"pango_lineage":"CHILD","primaryKey":"id_2"},
{"pango_lineage":"C","primaryKey":"id_3"},
{"pango_lineage":"CHILD.2","primaryKey":"id_4"},
{"pango_lineage":"RECOMBINANT","primaryKey":"id_5"}]
)"),
};

// RECOMBINANT's parents are not both below CHILD, so its clade ancestor is BASE.1.
const QueryTestScenario NOT_FULLY_CONTAINED_IN_CLADE = {
   .name = "NOT_FULLY_CONTAINED_IN_CLADE",
   .query =
      "data.filter(pango_lineage.lineageFromTables('CHILD', pango_lineage, pango_lineage_aliases, "
      "includeSublineages:=true, recombinantFollowingMode:='followIfFullyContainedInClade'))"
      ".project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"CHILD","primaryKey":"id_2"},
{"pango_lineage":"C","primaryKey":"id_3"}]
)"),
};

// Under the default 'doNotFollow', a recombinant lineage is reachable from none of its ancestors,
// so it only matches itself.
const QueryTestScenario RECOMBINANT_MATCHES_ITSELF = {
   .name = "RECOMBINANT_MATCHES_ITSELF",
   .query =
      "data.filter(pango_lineage.lineageFromTables('RECOMBINANT', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"RECOMBINANT","primaryKey":"id_5"}]
)"),
};

const QueryTestScenario ISOLATED_LINEAGE_MATCHES_ITSELF = {
   .name = "ISOLATED_LINEAGE_MATCHES_ITSELF",
   .query =
      "data.filter(pango_lineage.lineageFromTables('ISOLATED', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"ISOLATED","primaryKey":"id_6"}]
)"),
};

const QueryTestScenario LINEAGE_CONTAINING_QUOTE = {
   .name = "LINEAGE_CONTAINING_QUOTE",
   .query =
      "data.filter(pango_lineage.lineageFromTables('WITH''QUOTE', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"WITH'QUOTE","primaryKey":"id_7"}]
)"),
};

// The filter composes inside a boolean predicate.
const QueryTestScenario UNDER_AND = {
   .name = "UNDER_AND",
   .query =
      "data.filter(pango_lineage.lineageFromTables('BASE.1', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true) && "
      "(primaryKey = 'id_0' || primaryKey = 'id_4')).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"(
[{"pango_lineage":"BASE.1","primaryKey":"id_0"},
{"pango_lineage":"CHILD.2","primaryKey":"id_4"}]
)"),
};

// lineageFromTables is a rewrite of the query, so a lineage that is not in the tables just matches
// nothing, instead of being rejected like by lineage(...).
const QueryTestScenario UNKNOWN_LINEAGE_MATCHES_NOTHING = {
   .name = "UNKNOWN_LINEAGE_MATCHES_NOTHING",
   .query =
      "data.filter(pango_lineage.lineageFromTables('UNKNOWN', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true)).project({pango_lineage, primaryKey})",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario NULL_LINEAGE = {
   .name = "NULL_LINEAGE",
   .query =
      "data.filter(pango_lineage.lineageFromTables(null, pango_lineage, pango_lineage_aliases))"
      ".project({pango_lineage, primaryKey})",
   .expected_error_message =
      "lineageFromTables() does not accept null as the lineage; use isNull(\"pango_lineage\") "
      "instead",
};

const QueryTestScenario INVALID_RECOMBINANT_FOLLOWING_MODE = {
   .name = "INVALID_RECOMBINANT_FOLLOWING_MODE",
   .query =
      "data.filter(pango_lineage.lineageFromTables('BASE.1', pango_lineage, "
      "pango_lineage_aliases, includeSublineages:=true, recombinantFollowingMode:='sometimes'))"
      ".project({pango_lineage, primaryKey})",
   .expected_error_message =
      "invalid recombinantFollowingMode: 'sometimes'. Valid values are: alwaysFollow, "
      "followIfFullyContainedInClade, doNotFollow",
};

}  // namespace

QUERY_TEST(
   LineageFromTablesTest,
   TEST_DATA,
   ::testing::Values(
      CANONICAL_LINEAGE_MATCHES_ITS_ALIASES,
      ALIAS_MATCHES_ITS_CANONICAL_LINEAGE,
      INCLUDING_SUBLINEAGES,
      ALIAS_INCLUDING_SUBLINEAGES,
      ALWAYS_FOLLOW,
      FOLLOW_IF_FULLY_CONTAINED_IN_CLADE,
      NOT_FULLY_CONTAINED_IN_CLADE,
      RECOMBINANT_MATCHES_ITSELF,
      ISOLATED_LINEAGE_MATCHES_ITSELF,
      LINEAGE_CONTAINING_QUOTE,
      UNDER_AND,
      UNKNOWN_LINEAGE_MATCHES_NOTHING,
      NULL_LINEAGE,
      INVALID_RECOMBINANT_FOLLOWING_MODE
   )
)
