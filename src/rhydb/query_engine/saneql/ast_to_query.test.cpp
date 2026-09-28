#include "rhydb/query_engine/saneql/ast_to_query.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/parser.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::IllegalQueryException;
using rhydb::query_engine::saneql::convertToFilter;
using rhydb::query_engine::saneql::parseAndConvertToQueryTree;
using rhydb::query_engine::saneql::Parser;
namespace ast = rhydb::query_engine::saneql::ast;

namespace {

auto parseFilter(
   std::string_view query,
   const std::vector<rhydb::schema::ColumnIdentifier>& schema = {}
) {
   return convertToFilter(*Parser(query).parse(), schema, {});
}

// Schema exposing a nucleotide sequence column, so that the sequence leaf expressions can
// resolve their sequence name against the input schema at parse time.
const std::vector<rhydb::schema::ColumnIdentifier> SEQUENCE_SCHEMA{
   {.name = "segment1", .type = rhydb::schema::ColumnType::NUCLEOTIDE_SEQUENCE},
};

using Tables = std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>>;

Tables makeTablesWithDefault() {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   ColumnIdentifier date_column{.name = "date", .type = ColumnType::DATE32};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
      {date_column, std::make_shared<ColumnMetadata>(date_column.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   Tables tables;
   const rhydb::schema::TableName table_name("default");
   tables[table_name] = std::make_shared<rhydb::storage::Table>(table_name, schema);
   return tables;
}

/// A table whose `lineage` column is dictionary-encoded, next to the plain string `id` column.
Tables makeTablesWithDictionaryEncodedColumn() {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::DictionaryEncodedColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   ColumnIdentifier lineage_column{.name = "lineage", .type = ColumnType::DICTIONARY_ENCODED};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
      {lineage_column, std::make_shared<DictionaryEncodedColumnMetadata>(lineage_column.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   Tables tables;
   const rhydb::schema::TableName table_name("default");
   tables[table_name] = std::make_shared<rhydb::storage::Table>(table_name, schema);
   return tables;
}

// --- between ---

TEST(AstToQuery, nullFromAndTo) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree("default.filter(id.between(null, null))", tables);
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("Could not infer type of between expression. From-value or to-value "
                              "needs to be a typed non-null value, got from: 'null' to: 'null'")
      )
   );
}

// --- nucleotideEquals ---

TEST(AstToQueryNucleotideEquals, multiCharSymbolThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter("nucleotideEquals(position:=1, symbol:='ZZ', sequenceName:='segment1')");
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("nucleotideEquals() symbol must be a single character")
      )
   );
}

TEST(AstToQueryNucleotideEquals, invalidSymbolThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideEquals(position:=1, symbol:='Q', sequenceName:='segment1')", SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("nucleotideEquals() invalid symbol 'Q'")
      )
   );
}

// --- lineage ---

TEST(AstToQueryLineage, invalidRecombinantFollowingModeThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("lineage(clade, 'XBB', recombinantFollowingMode:='badMode')"); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("invalid recombinantFollowingMode: 'badMode'")
      )
   );
}

// --- nucleotideMutationProfile ---

TEST(AstToQueryMutationProfile, mutationsNotSetLiteralThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', mutations:='ACGT')",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "The 'mutations' argument of a Nucleotide MutationProfile expression must be a set "
         "literal"
      ))
   );
}

TEST(AstToQueryMutationProfile, mutationRecordPositionZeroThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', "
            "mutations:={{position:=0, symbol:='A'}})",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("value 0 is not allowed"))
   );
}

TEST(AstToQueryMutationProfile, mutationRecordMissingPositionThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', "
            "mutations:={{symbol:='A'}})",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("must have a 'position' field"))
   );
}

TEST(AstToQueryMutationProfile, mutationRecordMissingSymbolThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', "
            "mutations:={{position:=1}})",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("must have a 'symbol' field"))
   );
}

TEST(AstToQueryMutationProfile, mutationRecordMultiCharSymbolThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', "
            "mutations:={{position:=1, symbol:='AB'}})",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("must be a single character"))
   );
}

TEST(AstToQueryMutationProfile, mutationRecordInvalidSymbolThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', "
            "mutations:={{position:=1, symbol:='Q'}})",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("Invalid Nucleotide symbol 'Q' in MutationProfile")
      )
   );
}

TEST(AstToQueryMutationProfile, mutationListElementNotRecordThrows) {
   EXPECT_THAT(
      []() {
         (void)parseFilter(
            "nucleotideMutationProfile(distance:=1, sequenceName:='segment1', "
            "mutations:={'A123T'})",
            SEQUENCE_SCHEMA
         );
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("must be a record literal with 'position' and 'symbol' fields")
      )
   );
}

// --- convertToFilter ---

TEST(AstToQueryConvertToFilter, unknownScalarFunctionThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("unknownFunc(1)"); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("unknown scalar function 'unknownFunc'")
      )
   );
}

TEST(AstToQueryConvertToFilter, unsupportedExpressionTypeThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("42"); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("unsupported expression type in filter context")
      )
   );
}

TEST(AstToQueryConvertToFilter, booleanColumnReferenceBuildsFieldRef) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "isHuman", .type = rhydb::schema::ColumnType::BOOL},
   };
   EXPECT_EQ(parseFilter("isHuman", schema)->toString(), "isHuman");
}

TEST(AstToQueryConvertToFilter, unknownColumnReferenceThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("missing"); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("filter references unknown column 'missing'")
      )
   );
}

// --- integer comparisons ---

TEST(AstToQueryIntComparison, lessThanBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_EQ(parseFilter("age < 5", schema)->toString(), "age < 5");
}

TEST(AstToQueryIntComparison, greaterThanBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_EQ(parseFilter("age > 5", schema)->toString(), "age > 5");
}

// --- float comparisons ---

TEST(AstToQueryFloatComparison, lessEqualBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::FLOAT},
   };
   EXPECT_EQ(parseFilter("age <= 5.0", schema)->toString(), "age <= 5");
}

TEST(AstToQueryFloatComparison, greaterThanBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::FLOAT},
   };
   EXPECT_EQ(parseFilter("age > 5.0", schema)->toString(), "age > 5");
}

// --- date comparisons ---

TEST(AstToQueryDateComparison, lessThanBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "date", .type = rhydb::schema::ColumnType::DATE32},
   };
   EXPECT_EQ(parseFilter("date < '2020-01-01'::date", schema)->toString(), "date < '2020-01-01'");
}

TEST(AstToQueryDateComparison, greaterThanBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "date", .type = rhydb::schema::ColumnType::DATE32},
   };
   EXPECT_EQ(parseFilter("date > '2020-01-01'::date", schema)->toString(), "date > '2020-01-01'");
}

// --- convertBinaryExprToFilter ---

TEST(AstToQueryBinaryExpr, unsupportedValueTypeThrows) {
   // `a` is in the schema so that the right operand is the only invalid one and the
   // assertion cannot be satisfied by an error about the left side.
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "a", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_THAT(
      [&]() { (void)parseFilter("a = {1, 2}", schema); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("the right side of a comparison must be a literal value")
      )
   );
}

// Operands are converted in source order, so when both are invalid the leftmost one is
// reported. Pinning this keeps the diagnostic from depending on the compiler's choice of
// function-argument evaluation order.
TEST(AstToQueryBinaryExpr, bothOperandsInvalidReportsLeftOperand) {
   EXPECT_THAT(
      []() { (void)parseFilter("a = {1, 2}"); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("the left side of a comparison references unknown column 'a'")
      )
   );
}

// Equality is converted exactly like the ordering operators: both sides go through
// convertToScalar and a missing column reference is only caught later, when
// Comparison::compile looks for the column.

TEST(AstToQueryBinaryExpr, equalsNoIdentifierBuildsComparison) {
   EXPECT_EQ(parseFilter("'a' = 'b'")->toString(), "'a' = 'b'");
}

TEST(AstToQueryBinaryExpr, notEqualsNoIdentifierBuildsComparison) {
   EXPECT_EQ(parseFilter("'a' <> 'b'")->toString(), "'a' <> 'b'");
}

// `null` is not a comparable value for any operator, so it is rejected while
// converting the operand rather than being turned into a null test. Callers are
// expected to use isNull() / isNotNull() instead.

TEST(AstToQueryBinaryExpr, equalsNullThrows) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_THAT(
      [&]() { (void)parseFilter("age = null", schema); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("the right side of a comparison must be a literal value")
      )
   );
}

TEST(AstToQueryBinaryExpr, nullOnLeftThrows) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_THAT(
      [&]() { (void)parseFilter("null = age", schema); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("the left side of a comparison must be a literal value")
      )
   );
}

TEST(AstToQueryBinaryExpr, notEqualsNullThrows) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_THAT(
      [&]() { (void)parseFilter("age <> null", schema); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("the right side of a comparison must be a literal value")
      )
   );
}

TEST(AstToQueryBinaryExpr, orderingAgainstNullThrows) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_THAT(
      [&]() { (void)parseFilter("age < null", schema); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("the right side of a comparison must be a literal value")
      )
   );
}

TEST(AstToQueryBinaryExpr, equalsIdentifierOnRightBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   EXPECT_EQ(parseFilter("30 = age", schema)->toString(), "30 = age");
}

TEST(AstToQueryBinaryExpr, comparisonIdentifierOnRightBuildsComparison) {
   const std::vector<rhydb::schema::ColumnIdentifier> schema{
      {.name = "age", .type = rhydb::schema::ColumnType::INT32},
   };
   // Operands keep their written order; Comparison::compile flips the comparator when
   // the column is on the right, so `1 < age` stays `1 < age` at this stage.
   EXPECT_EQ(parseFilter("1 < age", schema)->toString(), "1 < age");
}

TEST(AstToQueryBinaryExpr, comparisonNoIdentifierBuildsComparison) {
   // A comparison without a column reference is not rejected at conversion time;
   // Comparison::compile catches the missing column later
   EXPECT_EQ(parseFilter("1 < 2")->toString(), "1 < 2");
}

TEST(AstToQueryBinaryExpr, unhandledBinaryOpThrows) {
   // All 8 BinaryOp enum values are handled in convertBinaryExprToFilter; the throw
   // after the switch is a defensive guard reachable only via an out-of-range op value.
   auto expr = ast::makeExpr(
      ast::BinaryExpr{
         .op = static_cast<ast::BinaryOp>(99),
         .left = ast::makeExpr(ast::Identifier{"a"}, {}),
         .right = ast::makeExpr(ast::IntLiteral{1}, {}),
      },
      {}
   );
   EXPECT_THAT(
      [&]() { (void)convertToFilter(*expr, {}, {}); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("unhandled binary operator"))
   );
}

// --- groupBy ---

TEST(AstToQueryGroupBy, aggregatesNotRecordLiteralThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&]() { (void)parseAndConvertToQueryTree("default.groupBy('not_a_record')", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("groupBy aggregates must be a record literal")
      )
   );
}

TEST(AstToQueryGroupBy, aggregateDefNotFunctionCallThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&]() { (void)parseAndConvertToQueryTree("default.groupBy({n:=42})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("aggregate definition 'n' must be a function call")
      )
   );
}

TEST(AstToQueryGroupBy, unknownAggregateFunctionThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&]() { (void)parseAndConvertToQueryTree("default.groupBy({n:=avg()})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("unknown aggregate function 'avg'. Valid functions: count, sum")
      )
   );
}

TEST(AstToQueryGroupBy, fieldNotInSchemaThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree("default.groupBy({n:=count()}, {nonexistent})", tables);
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "groupBy field 'nonexistent' is not present in the input's output schema"
      ))
   );
}

// --- project ---

TEST(AstToQueryProject, fieldNotInSchemaThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.project(nonexistent)", tables); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "project field 'nonexistent' is not present in the input's output schema"
      ))
   );
}

// --- map ---

TEST(AstToQueryMap, expressionsNotRecordLiteralThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map(id)", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("map() expects a record of assignments like {x := 3, y := age}")
      )
   );
}

TEST(AstToQueryMap, emptyBracesAreNotARecordLiteral) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map({})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("map() expects a record of assignments like {x := 3, y := age}")
      )
   );
}

TEST(AstToQueryMap, fieldReferenceResolvesToColumn) {
   auto tables = makeTablesWithDefault();
   EXPECT_NO_THROW((void)parseAndConvertToQueryTree("default.map({x := id})", tables));
}

TEST(AstToQueryMap, fieldReferenceToUnknownColumnThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map({x := nope})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("map() field 'x' references unknown column 'nope'")
      )
   );
}

TEST(AstToQueryMap, unsupportedValueThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map({x := count()})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("map() field 'x' references unknown scalar function 'count'")
      )
   );
}

TEST(AstToQueryMap, atResolvesToCharacterOfColumn) {
   auto tables = makeTablesWithDefault();
   EXPECT_NO_THROW((void)parseAndConvertToQueryTree("default.map({c := id.at(2)})", tables));
}

TEST(AstToQueryMap, atOnUnknownColumnThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map({c := nope.at(2)})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("at(): the field nope is not found in the current context")
      )
   );
}

TEST(AstToQueryMap, atWithPositionZeroThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map({c := id.at(0)})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("at(): the field 'position' is 1-indexed. Value of 0 not allowed.")
      )
   );
}

TEST(AstToQueryMap, isoWeekResolvesToWeekOfDateColumn) {
   auto tables = makeTablesWithDefault();
   const auto query_tree = parseAndConvertToQueryTree("default.map({w := date.isoWeek()})", tables);
   const auto output_schema = query_tree->getOutputSchema();
   const auto found =
      std::ranges::find_if(output_schema, [](const auto& col) { return col.name == "w"; });
   ASSERT_NE(found, output_schema.end());
   EXPECT_EQ(found->type, rhydb::schema::ColumnType::STRING);
}

TEST(AstToQueryMap, isoWeekOnUnknownColumnThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree("default.map({w := nope.isoWeek()})", tables);
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("isoWeek(): the field nope is not found in the current context")
      )
   );
}

TEST(AstToQueryMap, isoWeekOnNonDateColumnThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.map({w := id.isoWeek()})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("isoWeek(): the field id must be a date column")
      )
   );
}

TEST(AstToQueryFilter, nonBooleanScalarFunctionRejected) {
   EXPECT_THAT(
      []() {
         (void)parseFilter("id.at(2)", {{.name = "id", .type = rhydb::schema::ColumnType::STRING}});
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "scalar function 'at' produces a STRING value and cannot be used as a filter predicate"
      ))
   );
}

// --- orderBy ---

TEST(AstToQueryOrderBy, fieldUnsupportedTypeThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.orderBy({'value'})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("orderBy field must be an identifier or asc()/desc() call")
      )
   );
}

TEST(AstToQueryOrderBy, unsupportedFunctionNameThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.orderBy({foo(bar)})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("orderBy field must be an identifier or asc()/desc() call, got 'foo'")
      )
   );
}

TEST(AstToQueryOrderBy, ascWrongArgCountThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&]() { (void)parseAndConvertToQueryTree("default.orderBy({asc()})", tables); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("asc() expects exactly one argument"
      ))
   );
}

TEST(AstToQueryOrderBy, unknownFieldThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.orderBy({nonexistent})", tables); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("OrderByField nonexistent is not contained in the result")
      )
   );
}

TEST(AstToQueryOrderBy, unknownFieldInAscThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree("default.orderBy({asc(nonexistent)})", tables);
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("OrderByField nonexistent is not contained in the result")
      )
   );
}

// --- limit ---

TEST(AstToQueryLimit, zeroLimitThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() { (void)parseAndConvertToQueryTree("default.limit(0)", tables); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("limit must be a positive number"))
   );
}

// --- buildScanNode ---

TEST(AstToQueryBuildScanNode, tableNotFoundThrows) {
   EXPECT_THAT(
      []() { (void)parseAndConvertToQueryTree("unknown", {}); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("table 'unknown' not found in database")
      )
   );
}

// --- nOf ---

TEST(AstToQueryNOf, countNotIntegerThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("nOf('two', {country = 'Switzerland'})"); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("expected integer literal"))
   );
}

TEST(AstToQueryNOf, childrenNotSetLiteralThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("nOf(1, country = 'Switzerland')"); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("expected set literal"))
   );
}

TEST(AstToQueryNOf, matchExactlyNotBoolThrows) {
   EXPECT_THAT(
      []() { (void)parseFilter("nOf(1, {country = 'Switzerland'}, matchExactly:='yes')"); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("expected boolean literal"))
   );
}

// --- collectJoinKeys ---

TEST(AstToQueryJoin, onExpressionNotABinaryExpressionThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree(
            "join(default.project({id}), default.map({pk := id}).project({pk}), id)", tables
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "join() on-expression must be an equality between a left and a right column, or a "
         "conjunction (&&) of such equalities"
      ))
   );
}

TEST(AstToQueryJoin, conjunctOfOnExpressionNotABinaryExpressionThrows) {
   // The same check, but reached through the recursive descent into a '&&' conjunction.
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree(
            "join(default.project({id, date}), default.map({pk := id}).project({pk}), id = pk && "
            "date)",
            tables
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "join() on-expression must be an equality between a left and a right column, or a "
         "conjunction (&&) of such equalities"
      ))
   );
}

TEST(AstToQueryJoin, onExpressionNotAnEqualityThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree(
            "join(default.project({id}), default.map({pk := id}).project({pk}), id <> pk)", tables
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "join() on-expression only supports equality (=) comparisons, optionally combined with "
         "'&&'"
      ))
   );
}

TEST(AstToQueryJoin, onExpressionEqualityOfTwoColumnsOfTheSameInputThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree(
            "join(default.project({id, date}), default.map({pk := id}).project({pk}), id = date)",
            tables
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "join() on-expression equality must reference one column from each input, but both 'id' "
         "and 'date' resolve to the same input"
      ))
   );
}

TEST(AstToQueryJoin, onExpressionEqualityOfMismatchingColumnTypesThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         auto query_tree = parseAndConvertToQueryTree(
            "join(default.project({id}), default.map({num := 3}).project({num}), id = num)", tables
         );
      },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr(
         "join() on-expression equality must reference equal column types from each input, but "
         "'id' and 'num' have mismatching types STRING and INT64"
      ))
   );
}

// A dictionary-encoded column and a plain string column both materialize as an arrow utf8 array,
// so they are interchangeable as join keys - e.g. joining a transitiveClosure() result (always
// plain STRING) against an indexed lineage column, which is necessarily dictionary-encoded.
TEST(AstToQueryJoin, onExpressionEqualityOfStringAndDictionaryEncodedColumnsSucceeds) {
   auto tables = makeTablesWithDictionaryEncodedColumn();
   EXPECT_NO_THROW((void)parseAndConvertToQueryTree(
      "join(default.project({id}), default.map({lin := lineage}).project({lin}), id = lin)", tables
   ));
}

// --- resolveJoinColumn ---

TEST(AstToQueryJoin, onExpressionComparingANonIdentifierThrows) {
   auto tables = makeTablesWithDefault();
   EXPECT_THAT(
      [&tables]() {
         (void)parseAndConvertToQueryTree(
            "join(default.project({id}), default.map({pk := id}).project({pk}), id = 'value')",
            tables
         );
      },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("join() on-expression must compare column identifiers")
      )
   );
}

// --- convertExpression ---

TEST(AstToQueryConvertExpression, nonIdentifierNonFunctionCallThrows) {
   EXPECT_THAT(
      []() { (void)parseAndConvertToQueryTree("42", {}); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("expected table reference or function call")
      )
   );
}

}  // namespace
