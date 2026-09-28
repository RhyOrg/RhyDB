#include "rhydb/query_engine/scalar_expressions/nof.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/test/query_fixture.test.h"

namespace rhydb::query_engine::scalar_expressions {

// --- toString tests ---

TEST(NOfToString, shouldFormatAtLeastN) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<BoolLiteral>(true));
   children.emplace_back(std::make_unique<BoolLiteral>(true));
   children.emplace_back(std::make_unique<BoolLiteral>(true));

   const NOf nof(std::move(children), 2, false);

   EXPECT_EQ(nof.toString(), "[2-of:true, true, true]");
}

TEST(NOfToString, shouldFormatExactlyN) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<BoolLiteral>(true));
   children.emplace_back(std::make_unique<BoolLiteral>(true));

   const NOf nof(std::move(children), 1, true);

   EXPECT_EQ(nof.toString(), "[exactly-1-of:true, true]");
}

TEST(NOfToString, shouldHandleSingleChild) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<BoolLiteral>(true));

   const NOf nof(std::move(children), 1, false);

   EXPECT_EQ(nof.toString(), "[1-of:true]");
}

TEST(NOfToString, shouldHandleEmptyChildren) {
   ScalarExpressionVector children;

   const NOf nof(std::move(children), 0, false);

   EXPECT_EQ(nof.toString(), "[0-of:]");
}

}  // namespace rhydb::query_engine::scalar_expressions

// --- Query integration tests ---
namespace {

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createNOfData(
   const std::string& primary_key,
   const std::string& country,
   const std::string& region,
   const std::string& date
) {
   return nlohmann::json::parse(fmt::format(
      R"(
{{
   "primaryKey": "{}",
   "country": "{}",
   "region": "{}",
   "date": "{}",
   "segment1": {{
      "sequence": "ACGT",
      "insertions": []
   }}
}}
)",
      primary_key,
      country,
      region,
      date
   ));
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "country"
     type: "string"
     generateIndex: true
   - name: "region"
     type: "string"
     generateIndex: true
   - name: "date"
     type: "date"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ATGCN"}},
   {},
};

const QueryTestData NOF_TEST_DATA{
   .ndjson_input_data =
      {createNOfData("id_0", "Switzerland", "Europe", "2020-01-01"),
       createNOfData("id_1", "Germany", "Europe", "2021-06-15"),
       createNOfData("id_2", "USA", "Americas", "2019-03-20"),
       createNOfData("id_3", "Switzerland", "Europe", "2018-11-01"),
       createNOfData("id_4", "France", "Europe", "2022-07-10"),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

const QueryTestScenario NOF_AT_LEAST_2_OF_3 = {
   .name = "NOF_AT_LEAST_2_OF_3",
   .query =
      "default.filter(nOf(2, {country = 'Switzerland', country = 'Germany', region = "
      "'Europe'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Germany","primaryKey":"id_1"},
{"country":"Switzerland","primaryKey":"id_3"}
])"
   ),
};

const QueryTestScenario NOF_AT_LEAST_3_OF_3 = {
   .name = "NOF_AT_LEAST_3_OF_3",
   .query =
      "default"
      ".filter(nOf(3, {country = 'Switzerland', date = '2020-01-01'::date, region = 'Europe'}))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([
{"primaryKey":"id_0"}
])"),
};

const QueryTestScenario NOF_AT_LEAST_3_OF_3_EMPTY = {
   .name = "NOF_AT_LEAST_3_OF_3_EMPTY",
   .query =
      "default.filter(nOf(3, {country = 'Switzerland', country = 'Germany', region = 'Europe'}))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

const QueryTestScenario NOF_AT_LEAST_1_OF_2 = {
   .name = "NOF_AT_LEAST_1_OF_2",
   .query =
      "default.filter(nOf(1, {country = 'Switzerland', country = 'USA'}))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"USA","primaryKey":"id_2"},
{"country":"Switzerland","primaryKey":"id_3"}
])"
   ),
};

const QueryTestScenario NOF_EXACTLY_1_OF_2 = {
   .name = "NOF_EXACTLY_1_OF_2",
   .query =
      "default.filter(nOf(1, {country = 'Switzerland', region = 'Europe'}, matchExactly:=true))"
      ".project({primaryKey, country, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Germany","primaryKey":"id_1","region":"Europe"},
{"country":"France","primaryKey":"id_4","region":"Europe"}
])"
   ),
};

const QueryTestScenario NOF_EXACTLY_2_OF_2 = {
   .name = "NOF_EXACTLY_2_OF_2",
   .query =
      "default.filter(nOf(2, {country = 'Switzerland', region = 'Europe'}, matchExactly:=true))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Switzerland","primaryKey":"id_3"}
])"
   ),
};

const QueryTestScenario NOF_AT_LEAST_0 = {
   .name = "NOF_AT_LEAST_0",
   .query =
      "default.filter(nOf(0, {country = 'Switzerland', country = 'Germany'}))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"primaryKey":"id_0"},
{"primaryKey":"id_1"},
{"primaryKey":"id_2"},
{"primaryKey":"id_3"},
{"primaryKey":"id_4"}
])"
   ),
};

const QueryTestScenario NOF_EXACTLY_0 = {
   .name = "NOF_EXACTLY_0",
   .query =
      "default.filter(nOf(0, {country = 'Switzerland', region = 'Europe'}, matchExactly:=true))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"USA","primaryKey":"id_2"}
])"
   ),
};

const QueryTestScenario NOF_WITH_NEGATION = {
   .name = "NOF_WITH_NEGATION",
   .query =
      "default.filter(nOf(2, {country = 'Switzerland', !(country = 'USA')}))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Switzerland","primaryKey":"id_3"}
])"
   ),
};

// --- Gap coverage: handleOrCase with negated child (DeMorgan path) ---
// nOf(1, {country='Switzerland', !(country='Germany')})
// count=1, not exact, 2 children (1 non-negated, 1 negated) → handleOrCase with negated
// child 1: country=Switzerland → id_0, id_3
// child 2: NOT country=Germany → id_0, id_2, id_3, id_4
// at least 1: OR via DeMorgan → id_0, id_2, id_3, id_4
const QueryTestScenario NOF_OR_WITH_NEGATED_CHILD = {
   .name = "NOF_OR_WITH_NEGATED_CHILD",
   .query =
      "default.filter(nOf(1, {country = 'Switzerland', !(country = 'Germany')}))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"USA","primaryKey":"id_2"},
{"country":"Switzerland","primaryKey":"id_3"},
{"country":"France","primaryKey":"id_4"}
])"
   ),
};

// --- Gap coverage: exactly-0-of with 1 non-negated child (Complement) ---
// count=0, exact, 1 non-negated child → Complement(IndexScan)
const QueryTestScenario NOF_EXACTLY_0_SINGLE_NON_NEGATED = {
   .name = "NOF_EXACTLY_0_SINGLE_NON_NEGATED",
   .query =
      "default.filter(nOf(0, {country = 'Switzerland'}, matchExactly:=true))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Germany","primaryKey":"id_1"},
{"country":"USA","primaryKey":"id_2"},
{"country":"France","primaryKey":"id_4"}
])"
   ),
};

// --- Gap coverage: exactly-0-of with 1 negated child (return negated) ---
// count=0, exact, 1 negated child → return the inner (un-negated) operator directly
// NOT country=Switzerland compiles to Complement → mapChildExpressions negates it back →
// negated bucket has the raw IndexScan for country=Switzerland
// exactly-0 with 1 negated child means: the negated child must NOT match
const QueryTestScenario NOF_EXACTLY_0_SINGLE_NEGATED = {
   .name = "NOF_EXACTLY_0_SINGLE_NEGATED",
   .query =
      "default.filter(nOf(0, {!(country = 'Switzerland')}, matchExactly:=true))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Switzerland","primaryKey":"id_3"}
])"
   ),
};

// --- Gap coverage: handleAndCase all negated (Complement(Union)) ---
// Both children are negated → mapChildExpressions puts both in negated bucket
// count=2 == child_count=2 → handleAndCase, non_negated empty → Complement(Union(negated))
// child 1: NOT Switzerland → negated → IndexScan(Switzerland)
// child 2: NOT USA → negated → IndexScan(USA)
// handleAndCase: Complement(Union(Switzerland, USA)) = NOT(Switzerland OR USA)
const QueryTestScenario NOF_ALL_NEGATED_AND_CASE = {
   .name = "NOF_ALL_NEGATED_AND_CASE",
   .query =
      "default.filter(nOf(2, {!(country = 'Switzerland'), !(country = 'USA')}))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Germany","primaryKey":"id_1"},
{"country":"France","primaryKey":"id_4"}
])"
   ),
};

// --- Gap coverage: exactly-0-of with mixed negated/non-negated children (Intersection) ---
// "exactly 0 match" means neither child should be true:
//   country='Switzerland' must not match → not Switzerland
//   !(country='USA') must not match → must be USA
// Result: USA AND NOT Switzerland
const QueryTestScenario NOF_EXACTLY_0_MIXED_NEGATED = {
   .name = "NOF_EXACTLY_0_MIXED_NEGATED",
   .query =
      "default.filter(nOf(0, {country = 'Switzerland', !(country = 'USA')}, matchExactly:=true))"
      ".project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"USA","primaryKey":"id_2"}
])"
   ),
};

const QueryTestScenario NOF_EXACT_COUNT_EXCEEDS_CHILDREN = {
   .name = "NOF_EXACT_COUNT_EXCEEDS_CHILDREN",
   .query =
      "default.filter(nOf(5, {country = 'Switzerland', country = 'USA'}, matchExactly:=true))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

const QueryTestScenario NOF_AT_LEAST_COUNT_EXCEEDS_CHILDREN = {
   .name = "NOF_AT_LEAST_COUNT_EXCEEDS_CHILDREN",
   .query =
      "default.filter(nOf(5, {country = 'Switzerland', country = 'USA'}))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

// --- Gap coverage: single negated child, count=1 (Complement) ---
// count=1, 1 child (negated) → Complement of IndexScan(Switzerland)
const QueryTestScenario NOF_SINGLE_NEGATED_CHILD = {
   .name = "NOF_SINGLE_NEGATED_CHILD",
   .query = "default.filter(nOf(1, {!(country = 'Switzerland')})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Germany","primaryKey":"id_1"},
{"country":"USA","primaryKey":"id_2"},
{"country":"France","primaryKey":"id_4"}
])"
   ),
};

// --- Coverage: count=1, 1 non-negated child → return child directly ---
const QueryTestScenario NOF_SINGLE_NON_NEGATED_CHILD = {
   .name = "NOF_SINGLE_NON_NEGATED_CHILD",
   .query = "default.filter(nOf(1, {country = 'Switzerland'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Switzerland","primaryKey":"id_3"}
])"
   ),
};

// --- Gap coverage: exactly-1-of with 3 children → Threshold exact path ---
// count=1, exact, 3 non-trivial children → not handled by trivial/and/or cases → Threshold
// id_0: Switzerland=yes, Germany=no, Europe=yes → 2 matches → no
// id_1: Switzerland=no, Germany=yes, Europe=yes → 2 matches → no
// id_2: Switzerland=no, Germany=no, Europe=no → 0 matches → no
// id_3: Switzerland=yes, Germany=no, Europe=yes → 2 matches → no
// id_4: Switzerland=no, Germany=no, Europe=yes → 1 match → yes
const QueryTestScenario NOF_EXACTLY_1_OF_3_THRESHOLD = {
   .name = "NOF_EXACTLY_1_OF_3_THRESHOLD",
   .query =
      "default.filter(nOf(1, {country = 'Switzerland', country = 'Germany', region = 'Europe'}, "
      "matchExactly:=true)).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"France","primaryKey":"id_4"}
])"
   ),
};

// --- maybe(nOf(...)) exercises rewriteToNonExact decomposition end-to-end ---
// maybe(nOf(1, {country='Switzerland', region='Europe'}, matchExactly:=true))
// maybe triggers UPPER_BOUND rewrite → NOf exact with k < children.size() decomposes to
// And(nOf(1, ..., false), !nOf(2, ..., false))
// Same semantic result as NOF_EXACTLY_1_OF_2: exactly 1 of {Switzerland, Europe} must match
const QueryTestScenario NOF_MAYBE_EXACT_DECOMPOSITION = {
   .name = "NOF_MAYBE_EXACT_DECOMPOSITION",
   .query =
      "default.filter(maybe(nOf(1, {country = 'Switzerland', region = 'Europe'}, "
      "matchExactly:=true))).project({primaryKey, country, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Germany","primaryKey":"id_1","region":"Europe"},
{"country":"France","primaryKey":"id_4","region":"Europe"}
])"
   ),
};

// --- Coverage: all True children, count goes negative, !exact → Full ---
const QueryTestScenario NOF_ALL_TRUE_CHILDREN = {
   .name = "NOF_ALL_TRUE_CHILDREN",
   .query = "default.filter(nOf(2, {true, true, true})).project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"primaryKey":"id_0"},
{"primaryKey":"id_1"},
{"primaryKey":"id_2"},
{"primaryKey":"id_3"},
{"primaryKey":"id_4"}
])"
   ),
};

// --- Coverage: all True children, count goes negative, exact → Empty ---
const QueryTestScenario NOF_ALL_TRUE_EXACT_EMPTY = {
   .name = "NOF_ALL_TRUE_EXACT_EMPTY",
   .query = "default.filter(nOf(1, {true, true, true}, matchExactly:=true)).project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

// --- Coverage: all False children → all skipped, count > 0 remaining → Empty
const QueryTestScenario NOF_ALL_FALSE_CHILDREN = {
   .name = "NOF_ALL_FALSE_CHILDREN",
   .query = "default.filter(nOf(2, {false, false, false})).project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

// --- Coverage: mix of True/False/real children in mapChildExpressions ---
// True: count 1→0, False: skipped, Switzerland: non-negated bucket
// count=0, !exact → Full → all rows
const QueryTestScenario NOF_MIXED_TRIVIAL_AND_REAL = {
   .name = "NOF_MIXED_TRIVIAL_AND_REAL",
   .query = "default.filter(nOf(1, {true, false, country = 'Switzerland'})).project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"primaryKey":"id_0"},
{"primaryKey":"id_1"},
{"primaryKey":"id_2"},
{"primaryKey":"id_3"},
{"primaryKey":"id_4"}
])"
   ),
};

const QueryTestScenario NOF_NEGATIVE_COUNT = {
   .name = "NOF_NEGATIVE_COUNT",
   .query = "default.filter(nOf(-1, {country = 'Switzerland'})).project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"primaryKey":"id_0"},
{"primaryKey":"id_1"},
{"primaryKey":"id_2"},
{"primaryKey":"id_3"},
{"primaryKey":"id_4"}
])"
   ),
};

const QueryTestScenario NOF_NEGATIVE_COUNT_EXACT = {
   .name = "NOF_NEGATIVE_COUNT_EXACT",
   .query =
      "default.filter(nOf(-1, {country = 'Switzerland'}, matchExactly:=true))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

}  // namespace

QUERY_TEST(
   NOf,
   NOF_TEST_DATA,
   ::testing::Values(
      NOF_AT_LEAST_2_OF_3,
      NOF_AT_LEAST_3_OF_3,
      NOF_AT_LEAST_3_OF_3_EMPTY,
      NOF_AT_LEAST_1_OF_2,
      NOF_EXACTLY_1_OF_2,
      NOF_EXACTLY_2_OF_2,
      NOF_AT_LEAST_0,
      NOF_EXACTLY_0,
      NOF_WITH_NEGATION,
      NOF_OR_WITH_NEGATED_CHILD,
      NOF_EXACTLY_0_SINGLE_NON_NEGATED,
      NOF_EXACTLY_0_SINGLE_NEGATED,
      NOF_ALL_NEGATED_AND_CASE,
      NOF_EXACTLY_0_MIXED_NEGATED,
      NOF_EXACT_COUNT_EXCEEDS_CHILDREN,
      NOF_AT_LEAST_COUNT_EXCEEDS_CHILDREN,
      NOF_SINGLE_NEGATED_CHILD,
      NOF_SINGLE_NON_NEGATED_CHILD,
      NOF_EXACTLY_1_OF_3_THRESHOLD,
      NOF_MAYBE_EXACT_DECOMPOSITION,
      NOF_ALL_TRUE_CHILDREN,
      NOF_ALL_TRUE_EXACT_EMPTY,
      NOF_ALL_FALSE_CHILDREN,
      NOF_MIXED_TRIVIAL_AND_REAL,
      NOF_NEGATIVE_COUNT,
      NOF_NEGATIVE_COUNT_EXACT
   )
);
