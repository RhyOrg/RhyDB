#include <cstdint>
#include <optional>
#include <string>

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
   const std::string& aminoAcidSequence,
   const std::string& region,
   const std::string& country = "Germany",
   const std::string& date = "2021-01-04"
) {
   random_generator generator;
   const auto primary_key = generator();
   return {
      {"primaryKey", "id_" + to_string(primary_key)},
      {"region", region},
      {"country", country},
      {"date", date},
      {"unaligned_segment1", {}},
      {"segment1", {{"sequence", nucleotideSequence}, {"insertions", nlohmann::json::array()}}},
      {"gene1", {{"sequence", aminoAcidSequence}, {"insertions", nlohmann::json::array()}}},
   };
}

// Reference is "ATGCN" / "M*". Per nucleotide position the rows carry:
//   segment1[1]: A, A, N, C
//   segment1[2]: T, T, N, A
// so the (segment1[1], segment1[2]) combinations are (A,T)x2, (N,N)x1, (C,A)x1.
// The indexed `region` column carries: Europe, Europe, Asia, Europe.
// The plain (non-indexed) `country` column carries: Germany, France, Japan, Germany.
// The `date` column carries dates in ISO weeks 1, 10, 2, 2 of 2021 -- rendered by `isoWeek()` as
// the strings "2021-W01", "2021-W10", "2021-W02" (the zero-padded week keeps them chronologically
// sorted).
const nlohmann::json ROW_AT =
   createDataWithSequences("ATGCN", "M*", "Europe", "Germany", "2021-01-04");
const nlohmann::json ROW_AT2 =
   createDataWithSequences("ATGCN", "C*", "Europe", "France", "2021-03-08");
const nlohmann::json ROW_NN = createDataWithSequences("NNNNN", "M*", "Asia", "Japan", "2021-01-11");
const nlohmann::json ROW_CA =
   createDataWithSequences("CATTT", "X*", "Europe", "Germany", "2021-01-11");

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "region"
      type: "string"
      generateIndex: true
    - name: "country"
      type: "string"
    - name: "date"
      type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ATGCN"}},
   {{"gene1", "M*"}},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data = {ROW_AT, ROW_AT2, ROW_NN, ROW_CA},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// Mutation co-occurrence is an optimizer-only feature: it is expressed with the generic `at` scalar
// function (a `map` assigns the per-position symbols and `groupBy` groups on them), and the
// BitmapAggregationRewritePass recognizes that shape and routes it through the dedicated
// bitmap-based engine. Combinations are emitted depth-first in nucleotide SYMBOLS order (A before C
// before N).
const QueryTestScenario CO_OCCURRENCE_VIA_MAP_TWO_POSITIONS = {
   .name = "CO_OCCURRENCE_VIA_MAP_TWO_POSITIONS",
   .query =
      "default.map({s1 := segment1.at(1), s2 := segment1.at(2)})"
      ".groupBy({count:=count()}, {s1, s2})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "s2": "T", "count": 2},
      {"s1": "C", "s2": "A", "count": 1},
      {"s1": "N", "s2": "N", "count": 1}
   ])"),
};

const QueryTestScenario CO_OCCURRENCE_VIA_MAP_WITH_FILTER = {
   .name = "CO_OCCURRENCE_VIA_MAP_WITH_FILTER",
   .query =
      "default.filter(hasMutation(position:=1, sequenceName:='segment1'))"
      ".map({s1 := segment1.at(1), s2 := segment1.at(2)})"
      ".groupBy({count:=count()}, {s1, s2})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "C", "s2": "A", "count": 1}
   ])"),
};

const QueryTestScenario CO_OCCURRENCE_VIA_MAP_AMINO_ACID = {
   .name = "CO_OCCURRENCE_VIA_MAP_AMINO_ACID",
   .query = "default.map({stop := gene1.at(2)}).groupBy({count:=count()}, {stop})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"stop": "*", "count": 4}
   ])"),
};

// `at` on a non-sequence string column is not a sequence-position lookup, but it is still a general
// scalar expression the grouper can evaluate (extract a character), so it goes through the bitmap
// engine via the scalar-expression path. Every primary key starts with "id_", so the first
// character is 'i' for all four rows -- a single group.
const QueryTestScenario CO_OCCURRENCE_VIA_MAP_NON_SEQUENCE_STRING_AT = {
   .name = "CO_OCCURRENCE_VIA_MAP_NON_SEQUENCE_STRING_AT",
   .query = "default.map({first := primaryKey.at(1)}).groupBy({count:=count()}, {first})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"first": "i", "count": 4}
   ])"),
};

// A limit applied to an (unordered) aggregation used to be rejected outright because the group-by
// output carries no ordering. The FetchNode now marks such a result as implicitly ordered so the
// limit is honoured, accepting that the retained rows are an arbitrary subset. Grouping on
// primaryKey.at(1) yields a single group, keeping the truncated result deterministic here.
const QueryTestScenario LIMIT_ON_UNORDERED_AGGREGATION = {
   .name = "LIMIT_ON_UNORDERED_AGGREGATION",
   .query = "default.map({first := primaryKey.at(1)}).groupBy({count:=count()}, {first}).limit(1)",
   .expected_query_result = nlohmann::json::parse(R"([
      {"first": "i", "count": 4}
   ])"),
};

// The reference at segment1[5] is N, i.e. the local reference symbol is itself the missing symbol.
// The rows carry N, N, N, T there: the N group is every filtered row minus the explicit T mutation.
const QueryTestScenario CO_OCCURRENCE_VIA_MAP_REFERENCE_IS_MISSING = {
   .name = "CO_OCCURRENCE_VIA_MAP_REFERENCE_IS_MISSING",
   .query = "default.map({s5 := segment1.at(5)}).groupBy({count:=count()}, {s5})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s5": "T", "count": 1},
      {"s5": "N", "count": 3}
   ])"),
};

// The reference is only 5 symbols long, so position 6 is out of range. The rewritten bitmap
// aggregation node reports this when it builds the per-symbol bitmaps.
const QueryTestScenario CO_OCCURRENCE_VIA_MAP_POSITION_OUT_OF_RANGE = {
   .name = "CO_OCCURRENCE_VIA_MAP_POSITION_OUT_OF_RANGE",
   .query = "default.map({s := segment1.at(6)}).groupBy({count:=count()}, {s})",
   .expected_error_message =
      "segment1.at(6) is out of bounds: the nucleotide sequence has length 5",
};

// Grouping directly on an indexed string column is now routed through the bitmap engine too: the
// column is grouped straight from its inverted index. Value groups are emitted in sorted order
// (Asia before Europe). Region carries Europe x3 (the two ATGCN rows and the CATTT row) and Asia x1
// (the NNNNN row).
const QueryTestScenario INDEXED_COLUMN_SINGLE = {
   .name = "INDEXED_COLUMN_SINGLE",
   .query = "default.groupBy({count:=count()}, {region})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"region": "Asia", "count": 1},
      {"region": "Europe", "count": 3}
   ])"),
};

// A filter on an aggregate output column (`count`) must run above the groupBy, not be pushed into
// the scan where `count` does not exist.
const QueryTestScenario FILTER_ON_AGGREGATE_COUNT = {
   .name = "FILTER_ON_AGGREGATE_COUNT",
   .query = "default.groupBy({count:=count()}, {region}).filter(count > 1)",
   .expected_query_result = nlohmann::json::parse(R"([
      {"region": "Europe", "count": 3}
   ])"),
};

const QueryTestScenario FILTER_NOT_BY_COUNT_ON_AGGREGATE = {
   .name = "FILTER_NOT_BY_COUNT_ON_AGGREGATE",
   .query = "default.groupBy({count:=count()}, {region}).filter(region = 'Europe')",
   .expected_query_result = nlohmann::json::parse(R"([
      {"region": "Europe", "count": 3}
   ])"),
};

// A sequence position and an indexed column grouped together in one node. Depth-first over the
// nucleotide symbols at segment1[1] (A, C, N), with the region value groups sorted within each.
//   A -> Europe x2   (the two ATGCN rows)
//   C -> Europe x1   (the CATTT row)
//   N -> Asia   x1   (the NNNNN row)
const QueryTestScenario MIXED_SEQUENCE_AND_INDEXED_COLUMN = {
   .name = "MIXED_SEQUENCE_AND_INDEXED_COLUMN",
   .query = "default.map({s1 := segment1.at(1)}).groupBy({count:=count()}, {s1, region})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "region": "Europe", "count": 2},
      {"s1": "C", "region": "Europe", "count": 1},
      {"s1": "N", "region": "Asia", "count": 1}
   ])"),
};

// A bare field reference produced by the map (`r := region`, no `at`) over an *indexed* column is
// routed through the bitmap engine using the column's inverted index, exactly like grouping on the
// scan column directly -- only the output name differs (here `r`).
const QueryTestScenario MAP_FIELD_REF_INDEXED_COLUMN = {
   .name = "MAP_FIELD_REF_INDEXED_COLUMN",
   .query = "default.map({r := region}).groupBy({count:=count()}, {r})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"r": "Asia", "count": 1},
      {"r": "Europe", "count": 3}
   ])"),
};

// A bare field reference produced by the map over a *plain, non-indexed* string column, as the only
// grouping key. With no existing bitmap to reuse the rewrite declines and the generic Arrow
// aggregation handles it (its group order is unspecified, hence the orderBy); grouping it next to a
// bitmap-backed key goes through the bitmap engine instead (MIXED_SEQUENCE_AND_FIELD_COLUMN).
// Country carries Germany x2, France x1, Japan x1.
const QueryTestScenario MAP_FIELD_REF_PLAIN_STRING_COLUMN = {
   .name = "MAP_FIELD_REF_PLAIN_STRING_COLUMN",
   .query = "default.map({c := country}).groupBy({count:=count()}, {c}).orderBy({c.asc()})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"c": "France", "count": 1},
      {"c": "Germany", "count": 2},
      {"c": "Japan", "count": 1}
   ])"),
};

// A sequence position and a plain (scanned) string column grouped together in one node. Depth-first
// over the nucleotide symbols at segment1[1] (A, C, N), with the country values sorted within each.
//   A -> France x1 (ROW_AT2), Germany x1 (ROW_AT)
//   C -> Germany x1 (ROW_CA)
//   N -> Japan   x1 (ROW_NN)
const QueryTestScenario MIXED_SEQUENCE_AND_FIELD_COLUMN = {
   .name = "MIXED_SEQUENCE_AND_FIELD_COLUMN",
   .query = "default.map({s1 := segment1.at(1), c := country}).groupBy({count:=count()}, {s1, c})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "c": "France", "count": 1},
      {"s1": "A", "c": "Germany", "count": 1},
      {"s1": "C", "c": "Germany", "count": 1},
      {"s1": "N", "c": "Japan", "count": 1}
   ])"),
};

// A scalar-expression key under a filter: the grouper evaluates `country` only over the filtered
// rows, so the Asia row (ROW_NN, Japan) must not leak into any group.
//   A -> France x1 (ROW_AT2), Germany x1 (ROW_AT)
//   C -> Germany x1 (ROW_CA)
const QueryTestScenario MIXED_SEQUENCE_AND_FIELD_COLUMN_WITH_FILTER = {
   .name = "MIXED_SEQUENCE_AND_FIELD_COLUMN_WITH_FILTER",
   .query =
      "default.filter(region = 'Europe')"
      ".map({s1 := segment1.at(1), c := country})"
      ".groupBy({count:=count()}, {s1, c})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "c": "France", "count": 1},
      {"s1": "A", "c": "Germany", "count": 1},
      {"s1": "C", "c": "Germany", "count": 1}
   ])"),
};

// A general map-computed scalar expression, `date.isoWeek()`, as the only grouping key: like
// MAP_FIELD_REF_PLAIN_STRING_COLUMN this is left to the generic Arrow aggregation (hence the
// orderBy); MIXED_SEQUENCE_AND_ISO_WEEK covers it in the bitmap engine. The result is the ISO
// week-date string (`<ISO-year>-W<ISO-week>`), whose zero-padded week sorts chronologically.
//   isoWeek: 2021-W01 (ROW_AT), 2021-W10 (ROW_AT2), 2021-W02 (ROW_NN), 2021-W02 (ROW_CA)
const QueryTestScenario MAP_ISO_WEEK_EXPRESSION = {
   .name = "MAP_ISO_WEEK_EXPRESSION",
   .query =
      "default.map({week := date.isoWeek()}).groupBy({count:=count()}, {week})"
      ".orderBy({asc(week)})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"week": "2021-W01", "count": 1},
      {"week": "2021-W02", "count": 2},
      {"week": "2021-W10", "count": 1}
   ])"),
};

// A sequence position and an isoWeek expression grouped together in one node, mixing the sequence
// path with the scalar-expression path. Depth-first over segment1[1] (A, C, N), weeks sorted
// within.
//   A -> 2021-W01 (ROW_AT), 2021-W10 (ROW_AT2)
//   C -> 2021-W02 (ROW_CA)
//   N -> 2021-W02 (ROW_NN)
const QueryTestScenario MIXED_SEQUENCE_AND_ISO_WEEK = {
   .name = "MIXED_SEQUENCE_AND_ISO_WEEK",
   .query =
      "default.map({s1 := segment1.at(1), week := date.isoWeek()})"
      ".groupBy({count:=count()}, {s1, week})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "week": "2021-W01", "count": 1},
      {"s1": "A", "week": "2021-W10", "count": 1},
      {"s1": "C", "week": "2021-W02", "count": 1},
      {"s1": "N", "week": "2021-W02", "count": 1}
   ])"),
};

// A sequence-less row carries no symbol at any position. The generic `at()`/groupBy path emits a
// null group key for such a row, so the rewritten bitmap aggregation node must do the same instead
// of dropping the row or folding it into the missing symbol N/X. These scenarios pin that
// behaviour. segment1 reference is "ATGCN", gene1 reference is "M*".
//   NULL_ROW_A/B: segment1 = "ATGCN", gene1 = "M*"  (x2)
//   NULL_ROW_NO_NUC: segment1 absent, gene1 = "M*"
//   NULL_ROW_NO_AA:  segment1 = "CATTT", gene1 absent, region absent
nlohmann::json createDataWithOptionalSequences(
   const std::optional<std::string>& nucleotideSequence,
   const std::optional<std::string>& aminoAcidSequence,
   const std::optional<std::string>& region = "Europe"
) {
   random_generator generator;
   const auto primary_key = generator();
   const auto sequence_field = [](const std::optional<std::string>& sequence) -> nlohmann::json {
      if (sequence.has_value()) {
         return {{"sequence", sequence.value()}, {"insertions", nlohmann::json::array()}};
      }
      return nullptr;
   };
   return {
      {"primaryKey", "id_" + to_string(primary_key)},
      {"region", region.has_value() ? nlohmann::json(*region) : nlohmann::json()},
      {"country", "Germany"},
      {"date", "2021-01-04"},
      {"unaligned_segment1", {}},
      {"segment1", sequence_field(nucleotideSequence)},
      {"gene1", sequence_field(aminoAcidSequence)},
   };
}

const nlohmann::json NULL_ROW_A = createDataWithOptionalSequences("ATGCN", "M*");
const nlohmann::json NULL_ROW_B = createDataWithOptionalSequences("ATGCN", "M*");
const nlohmann::json NULL_ROW_NO_NUC = createDataWithOptionalSequences(std::nullopt, "M*");
const nlohmann::json NULL_ROW_NO_AA =
   createDataWithOptionalSequences("CATTT", std::nullopt, std::nullopt);

const QueryTestData NULL_TEST_DATA{
   .ndjson_input_data = {NULL_ROW_A, NULL_ROW_B, NULL_ROW_NO_NUC, NULL_ROW_NO_AA},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// segment1[1], segment1[2]: (A,T) for the two full rows, (C,A) for the row without an amino acid
// sequence, and (null,null) for the row without a nucleotide sequence.
const QueryTestScenario CO_OCCURRENCE_NULL_TWO_NUCLEOTIDE_POSITIONS = {
   .name = "CO_OCCURRENCE_NULL_TWO_NUCLEOTIDE_POSITIONS",
   .query =
      "default.map({s1 := segment1.at(1), s2 := segment1.at(2)})"
      ".groupBy({count:=count()}, {s1, s2})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "s2": "T", "count": 2},
      {"s1": "C", "s2": "A", "count": 1},
      {"s1": null, "s2": null, "count": 1}
   ])"),
};

// segment1[5] has the missing symbol N as its reference: the two full rows carry N, the row without
// an amino acid sequence carries T, and the row without a nucleotide sequence must land in the null
// group rather than the (reference) N group.
const QueryTestScenario CO_OCCURRENCE_NULL_REFERENCE_IS_MISSING = {
   .name = "CO_OCCURRENCE_NULL_REFERENCE_IS_MISSING",
   .query = "default.map({s5 := segment1.at(5)}).groupBy({count:=count()}, {s5})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s5": "T", "count": 1},
      {"s5": "N", "count": 2},
      {"s5": null, "count": 1}
   ])"),
};

// gene1[1] is the reference symbol M for the three rows that have an amino acid sequence, and null
// for the one that does not.
const QueryTestScenario CO_OCCURRENCE_NULL_AMINO_ACID = {
   .name = "CO_OCCURRENCE_NULL_AMINO_ACID",
   .query = "default.map({aa := gene1.at(1)}).groupBy({count:=count()}, {aa})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"aa": "M", "count": 3},
      {"aa": null, "count": 1}
   ])"),
};

// A combination across a nucleotide and an amino acid position: the null falls in different
// dimensions for the two partial rows, exercising a null key next to a present key.
//   full rows:        (A, M) x2
//   no amino acid:    (C, null)
//   no nucleotide:    (null, M)
const QueryTestScenario CO_OCCURRENCE_NULL_MIXED_POSITIONS = {
   .name = "CO_OCCURRENCE_NULL_MIXED_POSITIONS",
   .query =
      "default.map({s1 := segment1.at(1), aa := gene1.at(1)})"
      ".groupBy({count:=count()}, {s1, aa})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "aa": "M", "count": 2},
      {"s1": "C", "aa": null, "count": 1},
      {"s1": null, "aa": "M", "count": 1}
   ])"),
};

// The indexed `region` column is Europe for three rows and null for NULL_ROW_NO_AA. The null rows
// form a trailing group after every value group, emitted as a null key.
const QueryTestScenario INDEXED_COLUMN_NULL_GROUP = {
   .name = "INDEXED_COLUMN_NULL_GROUP",
   .query = "default.groupBy({count:=count()}, {region})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"region": "Europe", "count": 3},
      {"region": null, "count": 1}
   ])"),
};

// The bitmap aggregation node emits its combinations in pipeline-sized batches
// (materialization_cutoff is the batch-size-minus-one knob). With a cutoff of 0 every combination
// is its own batch, so this exercises the multi-batch producer path and pins that batching does not
// change or reorder the result.
const QueryTestScenario CO_OCCURRENCE_NULL_CHUNKED_OUTPUT = {
   .name = "CO_OCCURRENCE_NULL_CHUNKED_OUTPUT",
   .query =
      "default.map({s1 := segment1.at(1), s2 := segment1.at(2)})"
      ".groupBy({count:=count()}, {s1, s2})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "s2": "T", "count": 2},
      {"s1": "C", "s2": "A", "count": 1},
      {"s1": null, "s2": null, "count": 1}
   ])"),
   .query_options = rhydb::config::QueryOptions{.materialization_cutoff = 0},
};

const nlohmann::json AMBIGUITY_ROW_A = createDataWithSequences("ATGCN", "M*", "Europe");
const nlohmann::json AMBIGUITY_ROW_R1 = createDataWithSequences("RTGCN", "M*", "Europe");
const nlohmann::json AMBIGUITY_ROW_R2 = createDataWithSequences("RTGCN", "M*", "Europe");
const nlohmann::json AMBIGUITY_ROW_Y = createDataWithSequences("YTGCN", "M*", "Europe");

const QueryTestData AMBIGUITY_TEST_DATA{
   .ndjson_input_data = {AMBIGUITY_ROW_A, AMBIGUITY_ROW_R1, AMBIGUITY_ROW_R2, AMBIGUITY_ROW_Y},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario CO_OCCURRENCE_AMBIGUOUS_CODES = {
   .name = "CO_OCCURRENCE_AMBIGUOUS_CODES",
   .query = "default.map({s1 := segment1.at(1)}).groupBy({count:=count()}, {s1})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "A", "count": 1},
      {"s1": "R", "count": 2},
      {"s1": "Y", "count": 1}
   ])"),
};

// Every row's segment1 is fully missing (all N), so no row covers any position -- an all-N sequence
// carries a real (fully missing) coverage range, not a null. This exercises the whole-chunk "all
// missing" fast path: every filtered row collapses to the single missing-symbol group.
const nlohmann::json ALL_MISSING_ROW_A = createDataWithSequences("NNNNN", "M*", "Europe");
const nlohmann::json ALL_MISSING_ROW_B = createDataWithSequences("NNNNN", "M*", "Asia");
const nlohmann::json ALL_MISSING_ROW_C = createDataWithSequences("NNNNN", "M*", "Europe");

const QueryTestData ALL_MISSING_TEST_DATA{
   .ndjson_input_data = {ALL_MISSING_ROW_A, ALL_MISSING_ROW_B, ALL_MISSING_ROW_C},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

const QueryTestScenario ALL_ROWS_MISSING_AT_POSITION = {
   .name = "ALL_ROWS_MISSING_AT_POSITION",
   .query = "default.map({s1 := segment1.at(1)}).groupBy({count:=count()}, {s1})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s1": "N", "count": 3}
   ])"),
};

// ---------------------------------------------------------------------------
// One grouping key per remaining groupable scalar type
//
// The bitmap aggregation node instantiates a separate ValueTraits per output type -- key
// extraction, bucketing and the typed value array. Only the string and date ones were reached by a
// test; these cover int32, int64, float and bool. A map field reference over a non-indexed column
// is the only shape that gets a numeric or boolean expression to that path (`at` and `isoWeek`
// both yield strings). A scalar-only grouping is left to Arrow, so each scenario pairs it with the
// bitmap-backed `s := segment1.at(1)`, which is A for every row.
// ---------------------------------------------------------------------------

nlohmann::json createRowWithScalarTypes(
   std::optional<int32_t> age,
   std::optional<int64_t> reads,
   std::optional<double> coverage,
   std::optional<bool> passed
) {
   random_generator generator;
   const auto primary_key = generator();
   return {
      {"primaryKey", "id_" + to_string(primary_key)},
      {"age", age.has_value() ? nlohmann::json(*age) : nlohmann::json()},
      {"reads", reads.has_value() ? nlohmann::json(*reads) : nlohmann::json()},
      {"coverage", coverage.has_value() ? nlohmann::json(*coverage) : nlohmann::json()},
      {"passed", passed.has_value() ? nlohmann::json(*passed) : nlohmann::json()},
      {"unaligned_segment1", {}},
      {"segment1", {{"sequence", "ATGCN"}, {"insertions", nlohmann::json::array()}}},
      {"gene1", {{"sequence", "M*"}, {"insertions", nlohmann::json::array()}}},
   };
}

const auto SCALAR_TYPE_DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
    - name: "age"
      type: "int"
    - name: "reads"
      type: "int64"
    - name: "coverage"
      type: "float"
    - name: "passed"
      type: "boolean"
  primaryKey: "primaryKey"
)";

// Two rows share a value, one differs and one is null in every column, so each scenario sees a
// duplicate group, a singleton group and the null group. `reads` exceeds int32 so it genuinely
// needs the int64 traits, and the float values are exactly representable so there is no formatting
// ambiguity in the expected output.
const QueryTestData SCALAR_TYPE_TEST_DATA{
   .ndjson_input_data =
      {
         createRowWithScalarTypes(30, 1000000000000, 1.5, true),
         createRowWithScalarTypes(30, 1000000000000, 1.5, false),
         createRowWithScalarTypes(41, 2000000000000, 2.5, true),
         createRowWithScalarTypes(std::nullopt, std::nullopt, std::nullopt, std::nullopt),
      },
   .database_config = SCALAR_TYPE_DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
};

// Groups come out in ascending value order with the null group last, so the expected rows are
// written that way throughout.
const QueryTestScenario GROUP_BY_MAPPED_INT32_COLUMN = {
   .name = "GROUP_BY_MAPPED_INT32_COLUMN",
   .query = "default.map({s := segment1.at(1), a := age}).groupBy({count:=count()}, {s, a})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s": "A", "a": 30, "count": 2},
      {"s": "A", "a": 41, "count": 1},
      {"s": "A", "a": null, "count": 1}
   ])"),
};

const QueryTestScenario GROUP_BY_MAPPED_INT64_COLUMN = {
   .name = "GROUP_BY_MAPPED_INT64_COLUMN",
   .query = "default.map({s := segment1.at(1), r := reads}).groupBy({count:=count()}, {s, r})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s": "A", "r": 1000000000000, "count": 2},
      {"s": "A", "r": 2000000000000, "count": 1},
      {"s": "A", "r": null, "count": 1}
   ])"),
};

const QueryTestScenario GROUP_BY_MAPPED_FLOAT_COLUMN = {
   .name = "GROUP_BY_MAPPED_FLOAT_COLUMN",
   .query = "default.map({s := segment1.at(1), c := coverage}).groupBy({count:=count()}, {s, c})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s": "A", "c": 1.5, "count": 2},
      {"s": "A", "c": 2.5, "count": 1},
      {"s": "A", "c": null, "count": 1}
   ])"),
};

// false sorts before true, so the two boolean groups come out in that order.
const QueryTestScenario GROUP_BY_MAPPED_BOOL_COLUMN = {
   .name = "GROUP_BY_MAPPED_BOOL_COLUMN",
   .query = "default.map({s := segment1.at(1), p := passed}).groupBy({count:=count()}, {s, p})",
   .expected_query_result = nlohmann::json::parse(R"([
      {"s": "A", "p": false, "count": 1},
      {"s": "A", "p": true, "count": 2},
      {"s": "A", "p": null, "count": 1}
   ])"),
};

}  // namespace

QUERY_TEST(
   BitmapAggregation,
   TEST_DATA,
   ::testing::Values(
      CO_OCCURRENCE_VIA_MAP_TWO_POSITIONS,
      CO_OCCURRENCE_VIA_MAP_WITH_FILTER,
      CO_OCCURRENCE_VIA_MAP_AMINO_ACID,
      CO_OCCURRENCE_VIA_MAP_NON_SEQUENCE_STRING_AT,
      LIMIT_ON_UNORDERED_AGGREGATION,
      CO_OCCURRENCE_VIA_MAP_REFERENCE_IS_MISSING,
      CO_OCCURRENCE_VIA_MAP_POSITION_OUT_OF_RANGE,
      INDEXED_COLUMN_SINGLE,
      MIXED_SEQUENCE_AND_INDEXED_COLUMN,
      FILTER_ON_AGGREGATE_COUNT,
      FILTER_NOT_BY_COUNT_ON_AGGREGATE,
      MAP_FIELD_REF_INDEXED_COLUMN,
      MAP_FIELD_REF_PLAIN_STRING_COLUMN,
      MIXED_SEQUENCE_AND_FIELD_COLUMN,
      MIXED_SEQUENCE_AND_FIELD_COLUMN_WITH_FILTER,
      MAP_ISO_WEEK_EXPRESSION,
      MIXED_SEQUENCE_AND_ISO_WEEK
   )
);

QUERY_TEST(
   BitmapAggregationScalarTypes,
   SCALAR_TYPE_TEST_DATA,
   ::testing::Values(
      GROUP_BY_MAPPED_INT32_COLUMN,
      GROUP_BY_MAPPED_INT64_COLUMN,
      GROUP_BY_MAPPED_FLOAT_COLUMN,
      GROUP_BY_MAPPED_BOOL_COLUMN
   )
);

QUERY_TEST(
   BitmapAggregationNullSequences,
   NULL_TEST_DATA,
   ::testing::Values(
      CO_OCCURRENCE_NULL_TWO_NUCLEOTIDE_POSITIONS,
      CO_OCCURRENCE_NULL_AMINO_ACID,
      CO_OCCURRENCE_NULL_REFERENCE_IS_MISSING,
      CO_OCCURRENCE_NULL_MIXED_POSITIONS,
      CO_OCCURRENCE_NULL_CHUNKED_OUTPUT,
      INDEXED_COLUMN_NULL_GROUP
   )
);

QUERY_TEST(
   BitmapAggregationAmbiguousCodes,
   AMBIGUITY_TEST_DATA,
   ::testing::Values(CO_OCCURRENCE_AMBIGUOUS_CODES)
);

QUERY_TEST(
   BitmapAggregationAllMissing,
   ALL_MISSING_TEST_DATA,
   ::testing::Values(ALL_ROWS_MISSING_AT_POSITION)
);
