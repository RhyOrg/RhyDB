#include "rhydb/query_engine/scalar_expressions/or.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/scalar_expressions/comparison.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/string_in_set.h"
#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"
#include "rhydb/test/query_fixture.test.h"

namespace rhydb::query_engine::scalar_expressions {

namespace {

schema::ColumnIdentifier col(std::string name) {
   return {.name = std::move(name), .type = schema::ColumnType::STRING};
}

schema::ColumnIdentifier nucSeq(std::string name) {
   return {.name = std::move(name), .type = schema::ColumnType::NUCLEOTIDE_SEQUENCE};
}

// Helper to count expressions of a specific type
template <typename T>
size_t countExpressionsOfType(const ScalarExpressionVector& expressions) {
   size_t count = 0;
   for (const auto& expr : expressions) {
      if (dynamic_cast<T*>(expr.get()) != nullptr) {
         count++;
      }
   }
   return count;
}

}  // namespace

// Tests for mergeStringInSetExpressions

TEST(OrMergeStringInSet, shouldMergeTwoStringInSetWithSameColumn) {
   ScalarExpressionVector children;
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Switzerland"})
   );
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Germany"})
   );

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   ASSERT_EQ(result.size(), 1);
   auto* merged = dynamic_cast<StringInSet*>(result[0].get());
   ASSERT_NE(merged, nullptr);
   const std::string str = merged->toString();
   // Format is "column IN [value1,value2,...]"
   EXPECT_NE(str.find("country IN ["), std::string::npos);
   EXPECT_NE(str.find("Switzerland"), std::string::npos);
   EXPECT_NE(str.find("Germany"), std::string::npos);
}

TEST(OrMergeStringInSet, shouldKeepSeparateStringInSetWithDifferentColumns) {
   ScalarExpressionVector children;
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Switzerland"})
   );
   children.emplace_back(
      std::make_unique<StringInSet>(col("region"), std::unordered_set<std::string>{"Europe"})
   );

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   ASSERT_EQ(result.size(), 2);
   EXPECT_EQ(countExpressionsOfType<StringInSet>(result), 2);
}

TEST(OrMergeStringInSet, shouldPassThroughOtherExpressions) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<BoolLiteral>(true));
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Switzerland"})
   );
   children.emplace_back(std::make_unique<BoolLiteral>(true));

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   ASSERT_EQ(result.size(), 3);
   EXPECT_EQ(countExpressionsOfType<BoolLiteral>(result), 2);
   EXPECT_EQ(countExpressionsOfType<StringInSet>(result), 1);
}

TEST(OrMergeStringInSet, shouldHandleEmptyInput) {
   ScalarExpressionVector children;

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   EXPECT_TRUE(result.empty());
}

TEST(OrMergeStringInSet, shouldHandleSingleStringInSet) {
   ScalarExpressionVector children;
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Switzerland"})
   );

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   ASSERT_EQ(result.size(), 1);
   EXPECT_NE(dynamic_cast<StringInSet*>(result[0].get()), nullptr);
}

TEST(OrMergeStringInSet, shouldMergeMultipleValuesFromMultipleExpressions) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<StringInSet>(
      col("country"), std::unordered_set<std::string>{"Switzerland", "Austria"}
   ));
   children.emplace_back(std::make_unique<StringInSet>(
      col("country"), std::unordered_set<std::string>{"Germany", "France"}
   ));
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Italy"})
   );

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   ASSERT_EQ(result.size(), 1);
   auto* merged = dynamic_cast<StringInSet*>(result[0].get());
   ASSERT_NE(merged, nullptr);
   const std::string str = merged->toString();
   EXPECT_NE(str.find("Switzerland"), std::string::npos);
   EXPECT_NE(str.find("Germany"), std::string::npos);
   EXPECT_NE(str.find("France"), std::string::npos);
   EXPECT_NE(str.find("Austria"), std::string::npos);
   EXPECT_NE(str.find("Italy"), std::string::npos);
}

TEST(OrMergeStringInSet, shouldHandleDuplicateValues) {
   ScalarExpressionVector children;
   children.emplace_back(
      std::make_unique<StringInSet>(col("country"), std::unordered_set<std::string>{"Switzerland"})
   );
   children.emplace_back(std::make_unique<StringInSet>(
      col("country"), std::unordered_set<std::string>{"Switzerland", "Germany"}
   ));

   auto result = Or::mergeStringInSetExpressions(std::move(children));

   ASSERT_EQ(result.size(), 1);
   auto* merged = dynamic_cast<StringInSet*>(result[0].get());
   ASSERT_NE(merged, nullptr);
   const std::string str = merged->toString();
   EXPECT_NE(str.find("Switzerland"), std::string::npos);
   EXPECT_NE(str.find("Germany"), std::string::npos);
}

// Tests for rewriteSymbolInSetExpressions

TEST(OrRewriteSymbolInSet, shouldMergeTwoSymbolInSetWithSamePosition) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::G}
   ));

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   ASSERT_EQ(result.size(), 1);
   auto* merged = dynamic_cast<SymbolInSet<Nucleotide>*>(result[0].get());
   ASSERT_NE(merged, nullptr);
   // Format is "(sequence_name:symbol at position X in {A, G, ...})"
   const std::string str = merged->toString();
   EXPECT_NE(str.find("position 101"), std::string::npos);
   EXPECT_NE(str.find('A'), std::string::npos);
   EXPECT_NE(str.find('G'), std::string::npos);
}

TEST(OrRewriteSymbolInSet, shouldKeepSeparateSymbolInSetWithDifferentPositions) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 200, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::G}
   ));

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   ASSERT_EQ(result.size(), 2);
   EXPECT_EQ(countExpressionsOfType<SymbolInSet<Nucleotide>>(result), 2);
}

TEST(OrRewriteSymbolInSet, shouldKeepSeparateSymbolInSetWithDifferentSequenceNames) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment2"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::G}
   ));

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   ASSERT_EQ(result.size(), 2);
   EXPECT_EQ(countExpressionsOfType<SymbolInSet<Nucleotide>>(result), 2);
}

TEST(OrRewriteSymbolInSet, shouldPassThroughOtherExpressions) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<BoolLiteral>(true));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<BoolLiteral>(true));

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   ASSERT_EQ(result.size(), 3);
   EXPECT_EQ(countExpressionsOfType<BoolLiteral>(result), 2);
   EXPECT_EQ(countExpressionsOfType<SymbolInSet<Nucleotide>>(result), 1);
}

TEST(OrRewriteSymbolInSet, shouldHandleEmptyInput) {
   ScalarExpressionVector children;

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   EXPECT_TRUE(result.empty());
}

TEST(OrRewriteSymbolInSet, shouldMergeMultipleSymbolsFromMultipleExpressions) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"),
      100,
      std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::G, Nucleotide::Symbol::C}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::T}
   ));

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   ASSERT_EQ(result.size(), 1);
   auto* merged = dynamic_cast<SymbolInSet<Nucleotide>*>(result[0].get());
   ASSERT_NE(merged, nullptr);
   const std::string str = merged->toString();
   EXPECT_NE(str.find('A'), std::string::npos);
   EXPECT_NE(str.find('G'), std::string::npos);
   EXPECT_NE(str.find('C'), std::string::npos);
   EXPECT_NE(str.find('T'), std::string::npos);
}

TEST(OrRewriteSymbolInSet, shouldMergeOnlyMatchingPositionsAndSequences) {
   ScalarExpressionVector children;
   // These two should merge (same position and sequence name)
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 100, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::G}
   ));
   // This one stays separate (different position)
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 200, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::C}
   ));
   // These two should merge (same position and sequence name)
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 50, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
   ));
   children.emplace_back(std::make_unique<SymbolInSet<Nucleotide>>(
      nucSeq("segment1"), 50, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::T}
   ));

   auto result = Or::rewriteSymbolInSetExpressions<Nucleotide>(std::move(children));

   // Should have 3 SymbolInSet expressions:
   // 1. Merged A+G at position 100 (segment1)
   // 2. C at position 200 (segment1)
   // 3. Merged A+T at position 50 (segment1)
   ASSERT_EQ(result.size(), 3);
   EXPECT_EQ(countExpressionsOfType<SymbolInSet<Nucleotide>>(result), 3);
}

// Tests for Or::toString

TEST(OrToString, shouldFormatChildrenCorrectly) {
   ScalarExpressionVector children;
   children.emplace_back(std::make_unique<BoolLiteral>(true));
   children.emplace_back(std::make_unique<BoolLiteral>(true));

   const Or or_expression(std::move(children));

   EXPECT_EQ(or_expression.toString(), "Or(true | true)");
}

TEST(OrToString, shouldHandleNestedOr) {
   const storage::Table table(
      schema::TableName::getDefault(), std::make_shared<schema::TableSchema>()
   );

   ScalarExpressionVector inner_children;
   inner_children.emplace_back(std::make_unique<BoolLiteral>(true));
   inner_children.emplace_back(std::make_unique<BoolLiteral>(true));

   ScalarExpressionVector outer_children;
   outer_children.emplace_back(std::make_unique<Or>(std::move(inner_children)));
   outer_children.emplace_back(std::make_unique<BoolLiteral>(true));

   const Or outer_or(std::move(outer_children));

   EXPECT_EQ(outer_or.toString(), "Or(Or(true | true) | true)");

   auto rewritten_or = outer_or.rewrite(table, Or::AmbiguityMode::NONE);

   EXPECT_EQ(rewritten_or->toString(), "true");
}

using schema::ColumnIdentifier;
using schema::ColumnType;
using storage::column::ColumnMetadata;
using storage::column::StringColumnMetadata;

namespace {
std::unique_ptr<ScalarExpression> stringEquals(
   const std::string& column,
   const std::string& value
) {
   return std::make_unique<Comparison>(
      std::make_unique<FieldRef>(ColumnIdentifier{.name = column, .type = ColumnType::STRING}),
      std::make_unique<StringLiteral>(value),
      filter::operators::Comparator::EQUALS
   );
}
}  // namespace

TEST(OrToString, shouldHandleNestedStringEquals) {
   ColumnIdentifier primary_key{.name = "key", .type = ColumnType::STRING};
   const std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   const storage::Table table(
      schema::TableName::getDefault(),
      std::make_shared<schema::TableSchema>(column_metadata, primary_key)
   );

   ScalarExpressionVector inner_children;
   inner_children.emplace_back(stringEquals("key", "value_1"));
   inner_children.emplace_back(stringEquals("key", "value_2"));

   ScalarExpressionVector outer_children;
   outer_children.emplace_back(std::make_unique<Or>(std::move(inner_children)));
   outer_children.emplace_back(stringEquals("key", "value_3"));

   const Or outer_or(std::move(outer_children));

   EXPECT_EQ(outer_or.toString(), "Or(Or(key = 'value_1' | key = 'value_2') | key = 'value_3')");

   auto rewritten_or = outer_or.rewrite(table, Or::AmbiguityMode::NONE);

   EXPECT_EQ(rewritten_or->toString(), "key IN [value_1,value_2,value_3]");
}

TEST(OrToString, shouldHandleObufscatedNestedStringEquals) {
   ColumnIdentifier primary_key{.name = "key", .type = ColumnType::STRING};
   const std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   const storage::Table table(
      schema::TableName::getDefault(),
      std::make_shared<schema::TableSchema>(column_metadata, primary_key)
   );

   ScalarExpressionVector innermost_children;
   innermost_children.emplace_back(std::make_unique<BoolLiteral>(false));
   innermost_children.emplace_back(stringEquals("key", "value_1"));

   ScalarExpressionVector inner_children;
   inner_children.emplace_back(std::make_unique<Or>(std::move(innermost_children)));
   inner_children.emplace_back(stringEquals("key", "value_2"));

   ScalarExpressionVector outer_children;
   outer_children.emplace_back(std::make_unique<Or>(std::move(inner_children)));
   outer_children.emplace_back(stringEquals("key", "value_3"));

   const Or outer_or(std::move(outer_children));

   EXPECT_EQ(
      outer_or.toString(), "Or(Or(Or(false | key = 'value_1') | key = 'value_2') | key = 'value_3')"
   );

   auto rewritten_or = outer_or.rewrite(table, Or::AmbiguityMode::NONE);

   EXPECT_EQ(rewritten_or->toString(), "key IN [value_1,value_2,value_3]");
}

}  // namespace rhydb::query_engine::scalar_expressions

// Query tests for nested Or expressions and compiled trees
namespace {

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(const std::string& primary_key, const std::string& country) {
   return nlohmann::json::parse(fmt::format(
      R"(
{{
   "primaryKey": "{}",
   "country": "{}",
   "region": "{}",
   "segment1": {{
      "sequence": "ACGT",
      "insertions": []
   }}
}}
)",
      primary_key,
      country,
      country == "USA" ? "Americas" : "Europe"
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
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ATGCN"}},
   {},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {createData("id_0", "Switzerland"),
       createData("id_1", "Germany"),
       createData("id_2", "USA"),
       createData("id_3", "Switzerland"),
       createData("id_4", "France"),
       createData("id_5", "Germany"),},
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

const QueryTestScenario NESTED_OR_SAME_COLUMN = {
   .name = "NESTED_OR_SAME_COLUMN",
   .query =
      "default.filter((country = 'Switzerland' || country = 'Germany') || country = "
      "'France').project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Germany","primaryKey":"id_1"},
{"country":"Switzerland","primaryKey":"id_3"},
{"country":"France","primaryKey":"id_4"},
{"country":"Germany","primaryKey":"id_5"
}])"
   ),
};

const QueryTestScenario DEEPLY_NESTED_OR = {
   .name = "DEEPLY_NESTED_OR",
   .query =
      "default.filter(country = 'Switzerland' || country = 'Germany').project({primaryKey, "
      "country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Germany","primaryKey":"id_1"},
{"country":"Switzerland","primaryKey":"id_3"},
{"country":"Germany","primaryKey":"id_5"}
])"
   ),
};

const QueryTestScenario OR_SINGLE_CHILD_UNWRAPPED = {
   .name = "OR_SINGLE_CHILD_UNWRAPPED",
   .query = "default.filter(country = 'Switzerland').project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0"},{"country":"Switzerland","primaryKey":"id_3"}])"
   ),
};

const QueryTestScenario OR_STRING_EQUALS_MERGED = {
   .name = "OR_STRING_EQUALS_MERGED",
   .query =
      "default.filter(country = 'Switzerland' || country = 'Germany' || country = "
      "'France').project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0"},
{"country":"Germany","primaryKey":"id_1"},
{"country":"Switzerland","primaryKey":"id_3"},
{"country":"France","primaryKey":"id_4"},
{"country":"Germany","primaryKey":"id_5"}
])"
   ),
};

const QueryTestScenario OR_MIXED_COLUMNS = {
   .name = "OR_MIXED_COLUMNS",
   .query =
      "default.filter(country = 'USA' || region = 'Europe').project({primaryKey, country, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0","region":"Europe"},
{"country":"Germany","primaryKey":"id_1","region":"Europe"},
{"country":"USA","primaryKey":"id_2","region":"Americas"},
{"country":"Switzerland","primaryKey":"id_3","region":"Europe"},
{"country":"France","primaryKey":"id_4","region":"Europe"},
{"country":"Germany","primaryKey":"id_5","region":"Europe"}
])"
   ),
};

const QueryTestScenario OR_WITH_AND = {
   .name = "OR_WITH_AND",
   .query =
      "default.filter((country = 'Switzerland' && region = 'Europe') || country = "
      "'USA').project({primaryKey, country, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([
{"country":"Switzerland","primaryKey":"id_0","region":"Europe"},
{"country":"USA","primaryKey":"id_2","region":"Americas"},
{"country":"Switzerland","primaryKey":"id_3","region":"Europe"}
])"
   ),
};

const QueryTestScenario OR_EMPTY_CHILDREN = {
   .name = "OR_EMPTY_CHILDREN",
   .query = "default.filter(false).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

}  // namespace

QUERY_TEST(
   OrRewrite,
   TEST_DATA,
   ::testing::Values(
      NESTED_OR_SAME_COLUMN,
      DEEPLY_NESTED_OR,
      OR_SINGLE_CHILD_UNWRAPPED,
      OR_STRING_EQUALS_MERGED,
      OR_MIXED_COLUMNS,
      OR_WITH_AND,
      OR_EMPTY_CHILDREN
   )
);
