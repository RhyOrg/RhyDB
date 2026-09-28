#include <nlohmann/json.hpp>

#include "rhydb/preprocessing/lineage_definition_file.h"
#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::preprocessing::LineageDefinitionFile;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createDataWithLineageValue(const std::string& primaryKey, const std::string& value) {
   return {
      {"primaryKey", primaryKey},
      {"pango_lineage", value},
      {"pango_lineage_indexed", value},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

nlohmann::json createDataWithLineageNullValue(const std::string& primaryKey) {
   return {
      {"primaryKey", primaryKey},
      {"pango_lineage", nullptr},
      {"pango_lineage_indexed", nullptr},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

const std::vector<nlohmann::json> DATA = {
   createDataWithLineageValue("id_0", "BASE.1"),
   createDataWithLineageValue("id_1", "CHILD"),
   createDataWithLineageValue("id_2", "CHILD"),
   createDataWithLineageValue("id_3", "CHILD.2"),
   createDataWithLineageValue("id_4", "GRANDCHILD"),
   createDataWithLineageNullValue("id_5"),
};

// Two lineage columns:
//   * `pango_lineage` is a plain STRING column (no index) that carries each sequence's lineage.
//     transitiveClosure emits STRING from/to columns, and join() requires matching column
//     types, so the value we join the closure against must itself be STRING.
//   * `pango_lineage_indexed` has `generateLineageIndex` + `lineageIndexType: table`, which
//     materializes the companion `pango_lineage_indexed` relation table (columns
//     `lineage`/`parent`) whose closure we compute. A lineage-indexed column is necessarily
//     dictionary-encoded (generateIndex is required), which is why it cannot double as the
//     STRING join key.
const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "pango_lineage"
      type: "string"
    - name: "pango_lineage_indexed"
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

//   BASE.1
//   ├── CHILD
//   │   └── GRANDCHILD
//   └── CHILD.2
const auto LINEAGE_TREE =
   LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
BASE.1:
  parents: []
CHILD:
  parents:
  - BASE.1
CHILD.2:
  parents:
  - BASE.1
GRANDCHILD:
  parents:
  - CHILD
)"));

const QueryTestData TEST_DATA{
   .ndjson_input_data = DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .lineage_trees = {{"test_lineage_index", LINEAGE_TREE}},
};

// Plain transitive closure of the parent -> lineage edges: every (ancestor, descendant) pair.
const QueryTestScenario TRANSITIVE_CLOSURE_SCENARIO = {
   .name = "TRANSITIVE_CLOSURE_SCENARIO",
   .query = "pango_lineage_indexed.transitiveClosure('parent', 'lineage').orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "BASE.1"}, {"to", "CHILD"}},
       {{"from", "BASE.1"}, {"to", "CHILD.2"}},
       {{"from", "BASE.1"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},}
   ),
};

// Reflexive-transitive closure: additionally pairs every lineage with itself.
const QueryTestScenario TRANSITIVE_CLOSURE_INCLUDE_VERTICES_SCENARIO = {
   .name = "TRANSITIVE_CLOSURE_INCLUDE_VERTICES_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true)"
      ".orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "BASE.1"}, {"to", "BASE.1"}},
       {{"from", "BASE.1"}, {"to", "CHILD"}},
       {{"from", "BASE.1"}, {"to", "CHILD.2"}},
       {{"from", "BASE.1"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD"}, {"to", "CHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD.2"}, {"to", "CHILD.2"}},
       {{"from", "GRANDCHILD"}, {"to", "GRANDCHILD"}},}
   ),
};

// The motivating use case: count every lineage together with all of its sublineages.
// transitiveClosure (reflexive) -> join default on `to` = pango_lineage -> groupBy `from`.
//   BASE.1     : id_0, id_1, id_2, id_3, id_4 -> 5
//   CHILD      : id_1, id_2, id_4             -> 3
//   CHILD.2    : id_3                         -> 1
//   GRANDCHILD : id_4                         -> 1
const QueryTestScenario COUNT_LINEAGE_INCLUDING_SUBLINEAGES_SCENARIO = {
   .name = "COUNT_LINEAGE_INCLUDING_SUBLINEAGES_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true)"
      ".join(default, to = pango_lineage)"
      ".groupBy({count := count()}, {from})"
      ".orderBy({from})",
   .expected_query_result = nlohmann::json(
      {{{"from", "BASE.1"}, {"count", 5}},
       {{"from", "CHILD"}, {"count", 3}},
       {{"from", "CHILD.2"}, {"count", 1}},
       {{"from", "GRANDCHILD"}, {"count", 1}},}
   ),
};

// The input is any relation-producing subquery, not just a bare table: here a project() feeds
// transitiveClosure the two edge columns it reads.
const QueryTestScenario SUBQUERY_INPUT_SCENARIO = {
   .name = "SUBQUERY_INPUT_SCENARIO",
   .query =
      "pango_lineage_indexed.project({parent, lineage})"
      ".transitiveClosure('parent', 'lineage').orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "BASE.1"}, {"to", "CHILD"}},
       {{"from", "BASE.1"}, {"to", "CHILD.2"}},
       {{"from", "BASE.1"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},}
   ),
};

// A filter on the closure OUTPUT cannot be pushed into the source operator, so it is retained above
// transitiveClosure() and runs as an Arrow filter over the emitted {from, to} pairs (#1372). Keeps
// only the pairs reachable from BASE.1.
const QueryTestScenario FILTER_ON_OUTPUT_SCENARIO = {
   .name = "FILTER_ON_OUTPUT_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage')"
      ".filter(from = 'BASE.1').orderBy({from, to})",
   .expected_query_result = nlohmann::json({
      {{"from", "BASE.1"}, {"to", "CHILD"}},
      {{"from", "BASE.1"}, {"to", "CHILD.2"}},
      {{"from", "BASE.1"}, {"to", "GRANDCHILD"}},
   }),
};

// The closure is emitted in `materialization_cutoff + 1`-sized batches; with a cutoff of 0 every
// pair ends up in a batch of its own, which exercises the streaming path across several batches.
const QueryTestScenario TRANSITIVE_CLOSURE_ONE_PAIR_PER_BATCH_SCENARIO = {
   .name = "TRANSITIVE_CLOSURE_ONE_PAIR_PER_BATCH_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true)"
      ".orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "BASE.1"}, {"to", "BASE.1"}},
       {{"from", "BASE.1"}, {"to", "CHILD"}},
       {{"from", "BASE.1"}, {"to", "CHILD.2"}},
       {{"from", "BASE.1"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD"}, {"to", "CHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD.2"}, {"to", "CHILD.2"}},
       {{"from", "GRANDCHILD"}, {"to", "GRANDCHILD"}},}
   ),
   .query_options = rhydb::config::QueryOptions{.materialization_cutoff = 0},
};

// `startingFrom` restricts the sources: only the descendants of the named vertices are searched
// for and emitted, instead of the pairs of every vertex in the relation.
const QueryTestScenario STARTING_FROM_SCENARIO = {
   .name = "STARTING_FROM_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', startingFrom:={'CHILD'})"
      ".orderBy({from, to})",
   .expected_query_result = nlohmann::json({{{"from", "CHILD"}, {"to", "GRANDCHILD"}}}),
};

// includeVertices still adds the reflexive pair, but only for the requested sources.
const QueryTestScenario STARTING_FROM_INCLUDE_VERTICES_SCENARIO = {
   .name = "STARTING_FROM_INCLUDE_VERTICES_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true, "
      "startingFrom:={'CHILD', 'CHILD.2'}).orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "CHILD"}, {"to", "CHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD.2"}, {"to", "CHILD.2"}},}
   ),
};

// A requested vertex that does not occur in the relation contributes nothing, not even a
// reflexive pair.
const QueryTestScenario STARTING_FROM_UNKNOWN_VERTEX_SCENARIO = {
   .name = "STARTING_FROM_UNKNOWN_VERTEX_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true, "
      "startingFrom:={'NOT_A_LINEAGE'}).orderBy({from, to})",
   .expected_query_result = nlohmann::json::array(),
};

// The motivating use case narrowed to one lineage: count it together with all of its sublineages
// without computing the closure of the whole tree.
//   CHILD: id_1, id_2, id_4 -> 3
const QueryTestScenario COUNT_ONE_LINEAGE_INCLUDING_SUBLINEAGES_SCENARIO = {
   .name = "COUNT_ONE_LINEAGE_INCLUDING_SUBLINEAGES_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true, "
      "startingFrom:={'CHILD'})"
      ".join(default, to = pango_lineage)"
      ".groupBy({count := count()}, {from})"
      ".orderBy({from})",
   .expected_query_result = nlohmann::json({{{"from", "CHILD"}, {"count", 3}}}),
};

// `startingFrom` is a list of sources, not a deduplicated set: a vertex requested twice is
// searched twice, so every pair it contributes - the reflexive one included - is emitted twice.
const QueryTestScenario STARTING_FROM_DUPLICATE_VERTEX_SCENARIO = {
   .name = "STARTING_FROM_DUPLICATE_VERTEX_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true, "
      "startingFrom:={'CHILD', 'CHILD'}).orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "CHILD"}, {"to", "CHILD"}},
       {{"from", "CHILD"}, {"to", "CHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},
       {{"from", "CHILD"}, {"to", "GRANDCHILD"}},}
   ),
};

// The duplicated pairs reach whatever consumes the closure, so the sublineage count of a lineage
// requested twice is doubled: 2 * (id_1, id_2 for CHILD + id_4 for GRANDCHILD) -> 6, not 3.
// Callers that aggregate over the closure have to deduplicate `startingFrom` themselves.
const QueryTestScenario COUNT_LINEAGE_WITH_DUPLICATE_SOURCE_SCENARIO = {
   .name = "COUNT_LINEAGE_WITH_DUPLICATE_SOURCE_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true, "
      "startingFrom:={'CHILD', 'CHILD'})"
      ".join(default, to = pango_lineage)"
      ".groupBy({count := count()}, {from})"
      ".orderBy({from})",
   .expected_query_result = nlohmann::json({{{"from", "CHILD"}, {"count", 6}}}),
};

// An empty `startingFrom` is not the same as omitting it: it requests no sources at all, so the
// closure is empty rather than unrestricted.
const QueryTestScenario STARTING_FROM_EMPTY_SET_SCENARIO = {
   .name = "STARTING_FROM_EMPTY_SET_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', startingFrom:={})"
      ".orderBy({from, to})",
   .expected_query_result = nlohmann::json::array(),
};

// `includeVertices` does not include vertices that are not in the lineage tree
const QueryTestScenario STARTING_FROM_EMPTY_SET_INCLUDE_VERTICES_SCENARIO = {
   .name = "STARTING_FROM_EMPTY_SET_INCLUDE_VERTICES_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true, "
      "startingFrom:={}).orderBy({from, to})",
   .expected_query_result = nlohmann::json::array(),
};

const QueryTestScenario STARTING_FROM_NOT_A_SET_SCENARIO = {
   .name = "STARTING_FROM_NOT_A_SET_SCENARIO",
   .query = "pango_lineage_indexed.transitiveClosure('parent', 'lineage', startingFrom:='CHILD')",
   .expected_error_message = "expected set literal at 1:76",
};

const QueryTestScenario UNKNOWN_COLUMN_SCENARIO = {
   .name = "UNKNOWN_COLUMN_SCENARIO",
   .query = "pango_lineage_indexed.transitiveClosure('parent', 'does_not_exist')",
   .expected_error_message =
      "transitiveClosure() column 'does_not_exist' is not present in the input's output schema",
};

// A lineage-indexed column is dictionary-encoded, not STRING, so it cannot be an edge endpoint.
const QueryTestScenario NON_STRING_COLUMN_SCENARIO = {
   .name = "NON_STRING_COLUMN_SCENARIO",
   .query = "default.transitiveClosure('pango_lineage_indexed', 'primaryKey')",
   .expected_error_message =
      "transitiveClosure() can only be applied to STRING columns, but "
      "column 'pango_lineage_indexed' has type DICTIONARY_ENCODED",
};

//   ROOT
//   └── KID
//   ORPHAN
// ORPHAN is parentless and childless, so its row in the relation table has a null `parent` and
// contributes no edge at all - it is still a vertex of the relation.
const auto LINEAGE_TREE_WITH_ISOLATED_VERTEX =
   LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
ROOT:
  parents: []
KID:
  parents:
  - ROOT
ORPHAN:
  parents: []
)"));

const QueryTestData TEST_DATA_WITH_ISOLATED_VERTEX{
   .ndjson_input_data = {createDataWithLineageValue("id_0", "KID")},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .lineage_trees = {{"test_lineage_index", LINEAGE_TREE_WITH_ISOLATED_VERTEX}},
};

// An edge row with a null endpoint carries no edge, so the isolated vertex appears in no pair.
const QueryTestScenario ISOLATED_VERTEX_SCENARIO = {
   .name = "ISOLATED_VERTEX_SCENARIO",
   .query = "pango_lineage_indexed.transitiveClosure('parent', 'lineage').orderBy({from, to})",
   .expected_query_result = nlohmann::json({{{"from", "ROOT"}, {"to", "KID"}}}),
};

// ... but it is a vertex, so includeVertices still pairs it with itself.
const QueryTestScenario ISOLATED_VERTEX_INCLUDE_VERTICES_SCENARIO = {
   .name = "ISOLATED_VERTEX_INCLUDE_VERTICES_SCENARIO",
   .query =
      "pango_lineage_indexed.transitiveClosure('parent', 'lineage', includeVertices:=true)"
      ".orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "KID"}, {"to", "KID"}},
       {{"from", "ORPHAN"}, {"to", "ORPHAN"}},
       {{"from", "ROOT"}, {"to", "KID"}},
       {{"from", "ROOT"}, {"to", "ROOT"}},}
   ),
};

nlohmann::json createEdge(
   const std::string& primaryKey,
   const nlohmann::json& edge_from,
   const nlohmann::json& edge_to
) {
   return {
      {"primaryKey", primaryKey},
      {"edge_from", edge_from},
      {"edge_to", edge_to},
      {"weight", 1},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

// The relation is an arbitrary directed graph, not just a tree:
//   A <-> B  (a two-vertex cycle)   C -> C  (a self loop)   D -> E  (acyclic)   F -> null
const std::vector<nlohmann::json> GRAPH_DATA = {
   createEdge("id_0", "A", "B"),
   createEdge("id_1", "B", "A"),
   createEdge("id_2", "C", "C"),
   createEdge("id_3", "D", "E"),
   createEdge("id_4", "F", nullptr),
};

const auto GRAPH_DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "edge_from"
      type: "string"
    - name: "edge_to"
      type: "string"
    - name: "weight"
      type: "int"
  primaryKey: "primaryKey"
)";

const QueryTestData GRAPH_TEST_DATA{
   .ndjson_input_data = GRAPH_DATA,
   .database_config = GRAPH_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// A vertex on a cycle reaches itself, so it is paired with itself even without includeVertices.
const QueryTestScenario CYCLIC_RELATION_SCENARIO = {
   .name = "CYCLIC_RELATION_SCENARIO",
   .query =
      "default.project({edge_from, edge_to})"
      ".transitiveClosure('edge_from', 'edge_to').orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "A"}, {"to", "A"}},
       {{"from", "A"}, {"to", "B"}},
       {{"from", "B"}, {"to", "A"}},
       {{"from", "B"}, {"to", "B"}},
       {{"from", "C"}, {"to", "C"}},
       {{"from", "D"}, {"to", "E"}},}
   ),
};

// includeVertices adds the reflexive pair only for the vertices that no cycle already covers:
// (D, D), (E, E) and (F, F), but no second (A, A) / (B, B) / (C, C).
const QueryTestScenario CYCLIC_RELATION_INCLUDE_VERTICES_SCENARIO = {
   .name = "CYCLIC_RELATION_INCLUDE_VERTICES_SCENARIO",
   .query =
      "default.project({edge_from, edge_to})"
      ".transitiveClosure('edge_from', 'edge_to', includeVertices:=true)"
      ".orderBy({from, to})",
   .expected_query_result = nlohmann::json(
      {{{"from", "A"}, {"to", "A"}},
       {{"from", "A"}, {"to", "B"}},
       {{"from", "B"}, {"to", "A"}},
       {{"from", "B"}, {"to", "B"}},
       {{"from", "C"}, {"to", "C"}},
       {{"from", "D"}, {"to", "D"}},
       {{"from", "D"}, {"to", "E"}},
       {{"from", "E"}, {"to", "E"}},
       {{"from", "F"}, {"to", "F"}},}
   ),
};

const QueryTestScenario INT_COLUMN_SCENARIO = {
   .name = "INT_COLUMN_SCENARIO",
   .query = "default.transitiveClosure('edge_from', 'weight')",
   .expected_error_message =
      "transitiveClosure() can only be applied to STRING columns, but "
      "column 'weight' has type INT32",
};

nlohmann::json createDataWithSingleLineageColumn(
   const std::string& primaryKey,
   const nlohmann::json& value
) {
   return {
      {"primaryKey", primaryKey},
      {"pango_lineage", value},
      {"segment1", nullptr},
      {"unaligned_segment1", nullptr},
      {"gene1", nullptr},
   };
}

// A single lineage column with `lineageIndexType: both`, i.e. the realistic shape of a lineage
// config: there is no plain STRING copy of the lineage to join against, since a lineage-indexed
// column is necessarily dictionary-encoded. The closure emits STRING columns and join() compares
// join keys by the arrow type both sides materialize, so the two still join.
const auto SINGLE_LINEAGE_COLUMN_DATABASE_CONFIG =
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
      lineageIndexType: both
  primaryKey: "primaryKey"
)";

const QueryTestData SINGLE_LINEAGE_COLUMN_TEST_DATA{
   .ndjson_input_data =
      {createDataWithSingleLineageColumn("id_0", "BASE.1"),
       createDataWithSingleLineageColumn("id_1", "CHILD"),
       createDataWithSingleLineageColumn("id_2", "CHILD"),
       createDataWithSingleLineageColumn("id_3", "CHILD.2"),
       createDataWithSingleLineageColumn("id_4", "GRANDCHILD"),
       createDataWithSingleLineageColumn("id_5", nullptr),},
   .database_config = SINGLE_LINEAGE_COLUMN_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .lineage_trees = {{"test_lineage_index", LINEAGE_TREE}},
};

// The motivating query, joining the closure against the dictionary-encoded lineage column itself.
const QueryTestScenario COUNT_SUBLINEAGES_ON_DICTIONARY_ENCODED_COLUMN_SCENARIO = {
   .name = "COUNT_SUBLINEAGES_ON_DICTIONARY_ENCODED_COLUMN_SCENARIO",
   .query =
      "pango_lineage.transitiveClosure('parent', 'lineage', includeVertices:=true)"
      ".join(default, to = pango_lineage)"
      ".groupBy({count := count()}, {from})"
      ".orderBy({from})",
   .expected_query_result = nlohmann::json(
      {{{"from", "BASE.1"}, {"count", 5}},
       {{"from", "CHILD"}, {"count", 3}},
       {{"from", "CHILD.2"}, {"count", 1}},
       {{"from", "GRANDCHILD"}, {"count", 1}},}
   ),
};

}  // namespace

QUERY_TEST(
   TransitiveClosureTest,
   TEST_DATA,
   ::testing::Values(
      TRANSITIVE_CLOSURE_SCENARIO,
      TRANSITIVE_CLOSURE_INCLUDE_VERTICES_SCENARIO,
      TRANSITIVE_CLOSURE_ONE_PAIR_PER_BATCH_SCENARIO,
      COUNT_LINEAGE_INCLUDING_SUBLINEAGES_SCENARIO,
      SUBQUERY_INPUT_SCENARIO,
      FILTER_ON_OUTPUT_SCENARIO,
      STARTING_FROM_SCENARIO,
      STARTING_FROM_INCLUDE_VERTICES_SCENARIO,
      STARTING_FROM_UNKNOWN_VERTEX_SCENARIO,
      STARTING_FROM_DUPLICATE_VERTEX_SCENARIO,
      COUNT_LINEAGE_WITH_DUPLICATE_SOURCE_SCENARIO,
      STARTING_FROM_EMPTY_SET_SCENARIO,
      STARTING_FROM_EMPTY_SET_INCLUDE_VERTICES_SCENARIO,
      COUNT_ONE_LINEAGE_INCLUDING_SUBLINEAGES_SCENARIO,
      STARTING_FROM_NOT_A_SET_SCENARIO,
      UNKNOWN_COLUMN_SCENARIO,
      NON_STRING_COLUMN_SCENARIO
   )
);

QUERY_TEST(
   TransitiveClosureIsolatedVertexTest,
   TEST_DATA_WITH_ISOLATED_VERTEX,
   ::testing::Values(ISOLATED_VERTEX_SCENARIO, ISOLATED_VERTEX_INCLUDE_VERTICES_SCENARIO)
);

QUERY_TEST(
   TransitiveClosureGraphTest,
   GRAPH_TEST_DATA,
   ::testing::Values(
      CYCLIC_RELATION_SCENARIO,
      CYCLIC_RELATION_INCLUDE_VERTICES_SCENARIO,
      INT_COLUMN_SCENARIO
   )
);

QUERY_TEST(
   TransitiveClosureSingleLineageColumnTest,
   SINGLE_LINEAGE_COLUMN_TEST_DATA,
   ::testing::Values(COUNT_SUBLINEAGES_ON_DICTIONARY_ENCODED_COLUMN_SCENARIO)
);
