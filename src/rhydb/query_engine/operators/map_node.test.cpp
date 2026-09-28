#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primaryKey,
   int value,
   const std::string& str_value,
   const nlohmann::json& segment1 = nullptr,
   const nlohmann::json& unaligned_segment1 = nullptr,
   const std::string& date_value = ""
) {
   return {
      {"primaryKey", primaryKey},
      {"int_value", value},
      {"str_value", str_value},
      {"segment1", segment1},
      {"gene1", nullptr},
      {"unaligned_segment1", unaligned_segment1},
      {"date", date_value.empty() ? nlohmann::json(nullptr) : nlohmann::json(date_value)},
   };
}

nlohmann::json alignedSequence(const std::string& sequence) {
   return {{"sequence", sequence}, {"insertions", nlohmann::json::array()}};
}

// id_0 carries an aligned sequence, id_1 has no sequence (null) so that `at` on a
// sequence column can be checked for both a present and an absent value. The
// str_value column holds strings of differing lengths so that `at` can be checked
// for a position that is out of bounds for one value but in bounds for the other.
// unaligned_segment1 carries the (zstd-compressed) unaligned sequence used by the
// decompression scenarios.
const std::vector<nlohmann::json> DATA = {
   createData("id_0", 1, "short", alignedSequence("ACGT"), "ACGT", "2023-01-05"),
   createData("id_1", 2, "longlonglong", nullptr, nullptr, "2023-12-31"),
};

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "int_value"
      type: "int"
    - name: "str_value"
      type: "string"
    - name: "date"
      type: "date"
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
};

// Assigns a literal of each supported type to a new column.
const QueryTestScenario MAP_LITERALS_SCENARIO = {
   .name = "MAP_LITERALS",
   .query =
      R"(default.map({a := 3, b := 1.5, c := 'hello', d := true}).project({primaryKey, a, b, c, d}))",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"a", 3}, {"b", 1.5}, {"c", "hello"}, {"d", true}},
       {{"primaryKey", "id_1"}, {"a", 3}, {"b", 1.5}, {"c", "hello"}, {"d", true}},}
   ),
};

// An assignment whose name matches an existing column replaces it in place.
const QueryTestScenario MAP_OVERRIDE_SCENARIO = {
   .name = "MAP_OVERRIDE",
   .query = "default.map({int_value := 42}).project({primaryKey, int_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"int_value", 42}}, {{"primaryKey", "id_1"}, {"int_value", 42}}}
   ),
};

const QueryTestScenario MAP_OVERRIDE_TWICE_SCENARIO = {
   .name = "MAP_OVERRIDE_TWICE",
   .query = "default.map({int_value := 42}).map({int_value := 5}).project({primaryKey, int_value})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"int_value", 5}}, {{"primaryKey", "id_1"}, {"int_value", 5}}}
   ),
};

const QueryTestScenario MAP_INT64_SCENARIO = {
   .name = "MAP_INT64",
   .query = "default.map({x := 3000000000}).project({primaryKey, x})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"x", 3000000000}}, {{"primaryKey", "id_1"}, {"x", 3000000000}}}
   ),
};

// A column reference assigns an existing column's value to a new column.
const QueryTestScenario MAP_FIELD_REF_SCENARIO = {
   .name = "MAP_FIELD_REF",
   .query = "default.map({copied := int_value}).project({primaryKey, copied})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"copied", 1}}, {{"primaryKey", "id_1"}, {"copied", 2}}}
   ),
};

// All projected columns are produced by the map; none are passed through from
// the child. The table scan must still emit one row per input record (a prior
// bug narrowed the scan to zero fields and returned no rows).
const QueryTestScenario MAP_ONLY_MAPPED_COLUMN_SCENARIO = {
   .name = "MAP_ONLY_MAPPED_COLUMN",
   .query = "default.map({a := 1}).project({a})",
   .expected_query_result = nlohmann::json({{{"a", 1}}, {{"a", 1}}}),
};

// A map() that assigns the same output column twice is rejected at query construction
// time rather than silently dropping one assignment.
const QueryTestScenario MAP_DUPLICATE_OUTPUT_NAME_SCENARIO = {
   .name = "MAP_DUPLICATE_OUTPUT_NAME",
   .query = "default.map({x := 1, x := 2}).project({primaryKey, x})",
   .expected_query_result = {},
   .expected_error_message = "map() assigns the output column 'x' more than once",
};

// A table scan that exposes a (compressed) sequence column is wrapped in a MapNode
// holding a ZstdDecompressScalar assignment by wrapWithDecompressIfNeeded. This
// exercises the decompression path (backpressure sink/source + ProjectNode) of
// MapNode::addToExecPlan end-to-end.
const QueryTestScenario DECOMPRESS_SEQUENCE_SCENARIO = {
   .name = "DECOMPRESS_SEQUENCE",
   .query = "default.project({primaryKey, unaligned_segment1}).orderBy({primaryKey})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"unaligned_segment1", "ACGT"}},
       {{"primaryKey", "id_1"}, {"unaligned_segment1", nullptr}},}
   ),
};

// A user map() stacked on top of the implicit decompression MapNode: both the
// decompressed sequence column and a mapped literal column flow through.
const QueryTestScenario DECOMPRESS_WITH_USER_MAP_SCENARIO = {
   .name = "DECOMPRESS_WITH_USER_MAP",
   .query =
      "default.map({tag := 7}).project({primaryKey, unaligned_segment1, "
      "tag}).orderBy({primaryKey})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"unaligned_segment1", "ACGT"}, {"tag", 7}},
       {{"primaryKey", "id_1"}, {"unaligned_segment1", nullptr}, {"tag", 7}},}
   ),
};

// A nucleotide-symbol filter stacked on top of the implicit decompression MapNode for a
// sequence column, while ALSO projecting the (decompressed) aligned and unaligned sequences:
// Filter(nucleotideEquals(segment1)) -> Map(segment1/unaligned_segment1 := zstd_decompress(...)).
// Projecting the sequences keeps the decompression assignments alive through column narrowing,
// so the decompression map genuinely materializes them. The assignment is pushdown-transparent,
// so FilterPushdownPass still pushes the sequence predicate below the map into the scan, where
// SymbolEquals resolves `segment1` by name to the physical NUCLEOTIDE_SEQUENCE column.
const QueryTestScenario FILTER_NUCLEOTIDE_EQUALS_OVER_DECOMPRESS_MAP_SCENARIO = {
   .name = "FILTER_NUCLEOTIDE_EQUALS_OVER_DECOMPRESS_MAP",
   .query =
      "default.filter(nucleotideEquals(position := 1, symbol := 'A', sequenceName := 'segment1'))"
      ".project({primaryKey, segment1, unaligned_segment1})",
   .expected_query_result =
      nlohmann::json({{{"primaryKey", "id_0"}, {"segment1", "ACGT"}, {"unaligned_segment1", "ACGT"}},
      }),
};

// Regression guard: selecting the (zstd-compressed) sequence column together with a
// `limit` must still return the correct, order-stable result.
const QueryTestScenario DECOMPRESS_SEQUENCE_WITH_LIMIT_SCENARIO = {
   .name = "DECOMPRESS_SEQUENCE_WITH_LIMIT",
   .query = "default.project({primaryKey, unaligned_segment1}).orderBy({primaryKey}).limit(1)",
   .expected_query_result =
      nlohmann::json({{{"primaryKey", "id_0"}, {"unaligned_segment1", "ACGT"}}}),
};

// Correctness regression guard for a shape MapPullupPass is eligible to rewrite:
// `map({...}).limit(...)` builds a FetchNode directly above the user MapNode
// (Fetch(Map(scan))), which the pass may rewrite to Map(Fetch(scan)). QUERY_TEST asserts
// only the query result (it does not inspect the optimized plan), so this checks that the
// rewrite - when it happens - does not change the result.
const QueryTestScenario MAP_WITH_LIMIT_TRIGGERS_PULLUP_SCENARIO = {
   .name = "MAP_WITH_LIMIT_TRIGGERS_PULLUP",
   .query = "default.map({a := 3}).orderBy({primaryKey}).map({b := 7}).limit(1).project({a, b})",
   .expected_query_result = nlohmann::json({{{"a", 3}, {"b", 7}}}),
};

// --- map()/filter() ordering combinations ---
//
// FilterPushdownPass keeps a MapNode on top of a filter and pushes the filter down into the
// TableScan (effectively swapping Filter(Map)->Map(Filter)). These scenarios assert that both
// orderings produce the correct result (a filter that references a passed-through column, never
// a Map-produced one).

// filter() stacked on top of map(): Filter(Map(scan)). FilterPushdownPass keeps the Map on top
// and pushes the filter (on the passed-through `int_value`) down into the scan.
const QueryTestScenario FILTER_ON_TOP_OF_MAP_SCENARIO = {
   .name = "FILTER_ON_TOP_OF_MAP",
   .query = "default.map({a := 3}).filter(int_value = 1).project({primaryKey, a})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}, {"a", 3}}}),
};

// map() stacked on top of filter(): Map(Filter(scan)). The Map is already above the filter,
// so nothing is swapped; the filter is pushed to the scan.
const QueryTestScenario MAP_ON_TOP_OF_FILTER_SCENARIO = {
   .name = "MAP_ON_TOP_OF_FILTER",
   .query = "default.filter(int_value = 1).map({a := 3}).project({primaryKey, a})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}, {"a", 3}}}),
};

// A map() whose assignment reads a passed-through column, stacked below a filter on a
// different passed-through column.
const QueryTestScenario FILTER_ON_TOP_OF_MAP_FIELD_REF_SCENARIO = {
   .name = "FILTER_ON_TOP_OF_MAP_FIELD_REF",
   .query =
      "default.map({copied := int_value}).filter(str_value = 'short').project({primaryKey, "
      "copied})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}, {"copied", 1}}}),
};

// filter() over the implicit decompression MapNode: Filter(Map_decompress(scan)). The filter is
// pushed below the decompression Map into the scan, so only matching rows are decompressed.
const QueryTestScenario FILTER_OVER_DECOMPRESS_MAP_SCENARIO = {
   .name = "FILTER_OVER_DECOMPRESS_MAP",
   .query = "default.filter(int_value = 1).project({primaryKey, unaligned_segment1})",
   .expected_query_result =
      nlohmann::json({{{"primaryKey", "id_0"}, {"unaligned_segment1", "ACGT"}}}),
};

// filter() + user map() + implicit decompression map() + limit, all stacked. The filter is
// pushed below the Maps into the scan, and MapPullupPass then pulls the Maps above the fetch.
const QueryTestScenario FILTER_MAP_DECOMPRESS_LIMIT_SCENARIO = {
   .name = "FILTER_MAP_DECOMPRESS_LIMIT",
   .query =
      "default.filter(int_value >= 1).map({tag := 7}).project({primaryKey, unaligned_segment1, "
      "tag}).orderBy({primaryKey}).limit(1)",
   .expected_query_result =
      nlohmann::json({{{"primaryKey", "id_0"}, {"unaligned_segment1", "ACGT"}, {"tag", 7}}}),
};

// A filter that references a column the map produces (`a`) stays above the map and is executed as
// an Arrow filter exec node over the projected batch (see #1372). All rows carry `a = 3`.
const QueryTestScenario FILTER_ON_MAPPED_COLUMN_SCENARIO = {
   .name = "FILTER_ON_MAPPED_COLUMN",
   .query = "default.map({a := 3}).filter(a = 3).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}, {{"primaryKey", "id_1"}}}),
};

// #1371/#1372 verbatim: filtering on an isoWeek-derived column stays above the map and is executed
// as an Arrow filter over the mapped `week` column. Only id_0 falls into 2023-W01.
const QueryTestScenario FILTER_ON_ISO_WEEK_MAPPED_COLUMN_SCENARIO = {
   .name = "FILTER_ON_ISO_WEEK_MAPPED_COLUMN",
   .query = "default.map({week := date.isoWeek()}).filter(week = '2023-W01').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

// A filter on a column the map REPLACES in place with a value-changing expression (`str_value :=
// str_value.at(1)`) stays above the map and filters on the MAPPED value. id_0's "short".at(1) is
// 's' and matches; id_1's "longlonglong".at(1) is 'l' and does not.
const QueryTestScenario FILTER_ON_REPLACED_MAPPED_COLUMN_SCENARIO = {
   .name = "FILTER_ON_REPLACED_MAPPED_COLUMN",
   .query =
      "default.map({str_value := str_value.at(1)}).filter(str_value = 's').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

// A conjunction mixing a bitmap-only sequence predicate (hasMutation) with a derived column
// (`week`). The conjunction is split at the map boundary: the hasMutation conjunct is pushed into
// the table scan (bitmap path) while the `week` conjunct stays above the map and is realized as an
// Arrow filter, so the query composes and executes instead of erroring. No row has a mutation at
// segment1 position 1 (id_0 matches the reference, id_1 has no sequence), so the result is empty.
const QueryTestScenario FILTER_MIXED_PUSHABLE_AND_DERIVED_SCENARIO = {
   .name = "FILTER_MIXED_PUSHABLE_AND_DERIVED",
   .query =
      "default.map({week := date.isoWeek()})"
      ".filter(hasMutation(sequenceName := 'segment1', position := 1) && week = '2023-W01')"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::array(),
};

// A filter on a passed-through column (`int_value`) stacked above a map that produces an
// UNRELATED derived column (`week`). The filter is pushed to the scan and the unused `week` map
// is pruned by column narrowing; the query succeeds. Guards against over-eager map retention.
const QueryTestScenario FILTER_PUSHED_PAST_UNRELATED_DERIVED_MAP_SCENARIO = {
   .name = "FILTER_PUSHED_PAST_UNRELATED_DERIVED_MAP",
   .query = "default.map({week := date.isoWeek()}).filter(int_value = 1).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

// A `>` comparison on a map-produced column cannot be pushed below the map and is executed as an
// Arrow filter over the mapped `tag` column.
const QueryTestScenario FILTER_COMPARISON_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_COMPARISON_ABOVE_MAP",
   .query = "default.map({tag := int_value}).filter(tag > 1).project({primaryKey, tag})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}, {"tag", 2}}}),
};

// A conjunction of two comparisons on a map-produced column, executed as a single Arrow filter
// above the map.
const QueryTestScenario FILTER_AND_RANGE_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_AND_RANGE_ABOVE_MAP",
   .query = "default.map({tag := int_value}).filter(tag >= 2 && tag <= 2).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}}}),
};

// A disjunction over a map-produced column, executed as an Arrow filter above the map.
const QueryTestScenario FILTER_OR_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_OR_ABOVE_MAP",
   .query =
      "default.map({week := date.isoWeek()})"
      ".filter(week = '2023-W01' || week = '2023-W52').project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}, {{"primaryKey", "id_1"}}}),
};

const QueryTestScenario FILTER_NOT_EQUALS_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_NOT_EQUALS_ABOVE_MAP",
   .query = "default.map({tag := int_value}).filter(tag <> 1).project({primaryKey, tag})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}, {"tag", 2}}}),
};

const QueryTestScenario FILTER_LESS_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_LESS_ABOVE_MAP",
   .query = "default.map({tag := int_value}).filter(tag < 2).project({primaryKey, tag})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}, {"tag", 1}}}),
};

// A comparison against a date literal on a map-produced date column, executed as an Arrow filter
// above the map.
const QueryTestScenario FILTER_DATE_LITERAL_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_DATE_LITERAL_ABOVE_MAP",
   .query = "default.map({d := date}).filter(d > '2023-06-01'::date).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_1"}}}),
};

const QueryTestScenario FILTER_NESTED_AND_IN_OR_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_NESTED_AND_IN_OR_ABOVE_MAP",
   .query =
      "default.map({tag := int_value})"
      ".filter((tag >= 2 && tag <= 2) || tag = 1).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}, {{"primaryKey", "id_1"}}}),
};

// Negation is deliberately rejected in filters on subexpressions until the native null-semantics
// inconsistency (issue #1525) is resolved.
const QueryTestScenario FILTER_NEGATION_ABOVE_MAP_REJECTED_SCENARIO = {
   .name = "FILTER_NEGATION_ABOVE_MAP_REJECTED",
   .query =
      "default.map({week := date.isoWeek()})"
      ".filter(!(week = '2023-W01')).project({primaryKey})",
   .expected_error_message =
      "Error when planning query execution: NotImplemented: negation ('!') is not yet supported in "
      "filters on subexpressions (see GitHub issue #1525); apply the negation in a filter that is "
      "pushed into the table scan instead",
};

// `isoWeek` maps a date column to its ISO 8601 week date, `<ISO-year>-W<ISO-week>`, as a string.
const QueryTestScenario MAP_ISO_WEEK_SCENARIO = {
   .name = "MAP_ISO_WEEK",
   .query = "default.map({week := date.isoWeek()}).project({primaryKey, week})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"week", "2023-W01"}},
       {{"primaryKey", "id_1"}, {"week", "2023-W52"}},}
   ),
};

}  // namespace

QUERY_TEST(
   MapTest,
   TEST_DATA,
   ::testing::Values(
      MAP_LITERALS_SCENARIO,
      MAP_OVERRIDE_SCENARIO,
      MAP_OVERRIDE_TWICE_SCENARIO,
      MAP_INT64_SCENARIO,
      MAP_FIELD_REF_SCENARIO,
      MAP_ONLY_MAPPED_COLUMN_SCENARIO,
      MAP_DUPLICATE_OUTPUT_NAME_SCENARIO,
      DECOMPRESS_SEQUENCE_SCENARIO,
      DECOMPRESS_WITH_USER_MAP_SCENARIO,
      FILTER_NUCLEOTIDE_EQUALS_OVER_DECOMPRESS_MAP_SCENARIO,
      DECOMPRESS_SEQUENCE_WITH_LIMIT_SCENARIO,
      MAP_WITH_LIMIT_TRIGGERS_PULLUP_SCENARIO,
      FILTER_ON_TOP_OF_MAP_SCENARIO,
      MAP_ON_TOP_OF_FILTER_SCENARIO,
      FILTER_ON_TOP_OF_MAP_FIELD_REF_SCENARIO,
      FILTER_OVER_DECOMPRESS_MAP_SCENARIO,
      FILTER_MAP_DECOMPRESS_LIMIT_SCENARIO,
      FILTER_ON_MAPPED_COLUMN_SCENARIO,
      FILTER_ON_ISO_WEEK_MAPPED_COLUMN_SCENARIO,
      FILTER_ON_REPLACED_MAPPED_COLUMN_SCENARIO,
      FILTER_MIXED_PUSHABLE_AND_DERIVED_SCENARIO,
      FILTER_PUSHED_PAST_UNRELATED_DERIVED_MAP_SCENARIO,
      FILTER_COMPARISON_ABOVE_MAP_SCENARIO,
      FILTER_AND_RANGE_ABOVE_MAP_SCENARIO,
      FILTER_OR_ABOVE_MAP_SCENARIO,
      FILTER_NOT_EQUALS_ABOVE_MAP_SCENARIO,
      FILTER_LESS_ABOVE_MAP_SCENARIO,
      FILTER_DATE_LITERAL_ABOVE_MAP_SCENARIO,
      FILTER_NESTED_AND_IN_OR_ABOVE_MAP_SCENARIO,
      FILTER_NEGATION_ABOVE_MAP_REJECTED_SCENARIO,
      MAP_ISO_WEEK_SCENARIO
   )
);

namespace {

const std::vector<nlohmann::json> ISO_WEEK_NULL_DATA = {
   createData("id_0", 1, "short"),
   createData("id_1", 2, "short", nullptr, nullptr, "2020-12-31"),
};

const QueryTestData ISO_WEEK_NULL_TEST_DATA{
   .ndjson_input_data = ISO_WEEK_NULL_DATA,
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario MAP_ISO_WEEK_NULL_SCENARIO = {
   .name = "MAP_ISO_WEEK_NULL",
   .query = "default.map({week := date.isoWeek()}).project({primaryKey, week})",
   .expected_query_result = nlohmann::json(
      {{{"primaryKey", "id_0"}, {"week", nullptr}}, {{"primaryKey", "id_1"}, {"week", "2020-W53"}}}
   ),
};

// An isNull() predicate on a map-produced column, executed as an Arrow filter above the map. Only
// id_0 has no date, so its derived `week` is null.
const QueryTestScenario FILTER_ISNULL_ABOVE_MAP_SCENARIO = {
   .name = "FILTER_ISNULL_ABOVE_MAP",
   .query = "default.map({week := date.isoWeek()}).filter(week.isNull()).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

// Negation above a map runs on the Arrow (subexpression) path, which deliberately rejects negation
// until issue #1525 resolves the native null-semantics inconsistency.
const QueryTestScenario FILTER_NEGATION_ABOVE_MAP_NULL_REJECTED_SCENARIO = {
   .name = "FILTER_NEGATION_ABOVE_MAP_NULL_REJECTED",
   .query =
      "default.map({week := date.isoWeek()}).filter(!(week = '2020-W53')).project({primaryKey})",
   .expected_error_message =
      "Error when planning query execution: NotImplemented: negation ('!') is not yet supported in "
      "filters on subexpressions (see GitHub issue #1525); apply the negation in a filter that is "
      "pushed into the table scan instead",
};

// The same negation pushed into the table scan runs on the native bitmap path, which is unaffected
// by the rejection above: id_0 has a null date and is kept by the set-complement `!(date = ...)`.
const QueryTestScenario FILTER_NEGATION_KEEPS_NULL_PUSHED_DOWN_SCENARIO = {
   .name = "FILTER_NEGATION_KEEPS_NULL_PUSHED_DOWN",
   .query = "default.filter(!(date = '2020-12-31'::date)).project({primaryKey})",
   .expected_query_result = nlohmann::json({{{"primaryKey", "id_0"}}}),
};

}  // namespace

QUERY_TEST(
   MapIsoWeekNullTest,
   ISO_WEEK_NULL_TEST_DATA,
   ::testing::Values(
      MAP_ISO_WEEK_NULL_SCENARIO,
      FILTER_ISNULL_ABOVE_MAP_SCENARIO,
      FILTER_NEGATION_ABOVE_MAP_NULL_REJECTED_SCENARIO,
      FILTER_NEGATION_KEEPS_NULL_PUSHED_DOWN_SCENARIO
   )
);
