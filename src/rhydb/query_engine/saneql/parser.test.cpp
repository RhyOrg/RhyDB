#include "rhydb/query_engine/saneql/parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/parse_exception.h"

using rhydb::query_engine::saneql::ParseException;
using rhydb::query_engine::saneql::Parser;
namespace ast = rhydb::query_engine::saneql::ast;

TEST(SaneQLParser, parsesIdentifier) {
   Parser parser("country");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "country");
}

TEST(SaneQLParser, parsesStringLiteral) {
   Parser parser("'USA'");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "'USA'");
}

TEST(SaneQLParser, parsesIntLiteral) {
   Parser parser("42");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "42");
}

TEST(SaneQLParser, parsesNegativeIntLiteral) {
   Parser parser("-42");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::IntLiteral>(expr->value));
   EXPECT_EQ(std::get<ast::IntLiteral>(expr->value).value, -42);
   EXPECT_EQ(expr->toString(), "-42");
}

TEST(SaneQLParser, parsesFloatLiteral) {
   Parser parser("3.14");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FloatLiteral>(expr->value));
   EXPECT_DOUBLE_EQ(std::get<ast::FloatLiteral>(expr->value).value, 3.14);
}

TEST(SaneQLParser, parsesNegativeFloat) {
   Parser parser("-3.14");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FloatLiteral>(expr->value));
   EXPECT_DOUBLE_EQ(std::get<ast::FloatLiteral>(expr->value).value, -3.14);
}

TEST(SaneQLParser, parsesBoolLiteral) {
   Parser parser("true");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "true");
}

TEST(SaneQLParser, parsesNullLiteral) {
   Parser parser("null");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "null");
}

TEST(SaneQLParser, parsesEquality) {
   Parser parser("country = 'USA'");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "(country = 'USA')");
}

TEST(SaneQLParser, parsesNotEquals) {
   Parser parser("country <> 'USA'");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "(country <> 'USA')");
}

TEST(SaneQLParser, parsesAndExpression) {
   Parser parser("a = 1 && b = 2");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "((a = 1) && (b = 2))");
}

TEST(SaneQLParser, parsesOrExpression) {
   Parser parser("a = 1 || b = 2");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "((a = 1) || (b = 2))");
}

TEST(SaneQLParser, parsesNotExpression) {
   Parser parser("!active");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "(!active)");
}

TEST(SaneQLParser, parsesAndOrPrecedence) {
   Parser parser("a = 1 && b = 2 || c = 3");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "(((a = 1) && (b = 2)) || (c = 3))");
}

TEST(SaneQLParser, parsesParenthesizedExpression) {
   Parser parser("(a || b) && c");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "((a || b) && c)");
}

TEST(SaneQLParser, parsesFunctionCall) {
   Parser parser("hasMutation('A123T')");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "hasMutation('A123T')");
}

TEST(SaneQLParser, parsesFunctionCallWithNamedArgs) {
   Parser parser("hasMutation(position:=1000, sequenceName:='segment1')");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "hasMutation(position:=1000, sequenceName:='segment1')");
}

TEST(SaneQLParser, parsesMethodCall) {
   Parser parser("default.filter(country = 'USA')");
   auto expr = parser.parse();
   // Method call syntax is desugared: receiver becomes first positional arg
   EXPECT_EQ(expr->toString(), "filter(default, (country = 'USA'))");
}

TEST(SaneQLParser, parsesMethodCallChain) {
   Parser parser("default.filter(country = 'USA').group(by:={}, aggs:={count:=count()})");
   auto expr = parser.parse();
   EXPECT_EQ(
      expr->toString(), "group(filter(default, (country = 'USA')), by:={}, aggs:={count:=count()})"
   );
}

TEST(SaneQLParser, parsesTypeCast) {
   Parser parser("'2020-01-01'::date");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "'2020-01-01'::date");
}

TEST(SaneQLParser, parsesSetLiteral) {
   Parser parser("{'USA', 'Germany', 'France'}");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "{'USA', 'Germany', 'France'}");
}

TEST(SaneQLParser, parsesEmptySetLiteral) {
   Parser parser("{}");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "{}");
}

TEST(SaneQLParser, parsesMethodCallOnSetLiteral) {
   Parser parser("country.in({'USA', 'Germany'})");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "in(country, {'USA', 'Germany'})");
}

TEST(SaneQLParser, parsesComplexFilterQuery) {
   Parser parser("default.filter(country = 'USA' && age > 30).group(by:={}, aggs:={count:=count()})"
   );
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   auto& outer = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(outer.function_name, "group");
   // First positional arg is the child pipeline (filter call)
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(outer.positional_arguments[0].value->value)
   );
   auto& filter = std::get<ast::FunctionCall>(outer.positional_arguments[0].value->value);
   EXPECT_EQ(filter.function_name, "filter");
}

TEST(SaneQLParser, parsesDateBetweenWithTypeCast) {
   Parser parser("date_submitted.between('2020-01-01'::date, '2023-12-31'::date)");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   auto& call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(call.function_name, "between");
   // 3 positional arguments: receiver (date_submitted) + 2 date args
   ASSERT_EQ(call.positional_arguments.size(), 3);
}

TEST(SaneQLParser, parsesLimitMethod) {
   Parser parser("default.filter(country = 'USA').limit(100)");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   auto& limit_call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(limit_call.function_name, "limit");
   // 2 positional arguments: receiver (filter(...)) + 100
   ASSERT_EQ(limit_call.positional_arguments.size(), 2);
}

TEST(SaneQLParser, parsesComparisonOperators) {
   Parser parser("age < 30");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::BinaryExpr>(expr->value));
   auto& bin = std::get<ast::BinaryExpr>(expr->value);
   EXPECT_EQ(bin.op, ast::BinaryOp::LESS_THAN);
}

TEST(SaneQLParser, parsesFullExampleQuery) {
   Parser parser(
      "metadata\n"
      "  .filter(country = 'USA' && date_submitted.between('2020-01-01'::date, "
      "'2023-12-31'::date))\n"
      "  .group(by:={}, aggs:={count:=count()})"
   );
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   auto& agg = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(agg.function_name, "group");
}

TEST(SaneQLParser, throwsOnUnexpectedToken) {
   EXPECT_THAT(
      []() {
         Parser parser("= 'broken'");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Unexpected token Equals")
      )
   );
}

TEST(SaneQLParser, throwsOnMissingClosingParen) {
   EXPECT_THAT(
      []() {
         Parser parser("func(a, b");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:10: Expected RightParen but got Eof")
      )
   );
}

TEST(SaneQLParser, throwsOnTrailingGarbage) {
   EXPECT_THAT(
      []() {
         Parser parser("a b");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:3: Expected Eof but got Identifier")
      )
   );
}

TEST(SaneQLParser, throwsOnInvalidRecordLiteral) {
   EXPECT_THAT(
      []() {
         Parser parser("{a.b := 'c'}");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: A RecordLiteral requires a simple identifier "
                              "expression as field names")
      )
   );
}

TEST(SaneQLParser, throwsOnInvalidNamedArgument) {
   EXPECT_THAT(
      []() {
         Parser parser("test(1, x(1) := false)");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:9: A named argument requires a simple identifier "
                              "as the name, but got 'x(1)'")
      )
   );
}

TEST(SaneQLParser, throwsOnRepeatedComparisons) {
   EXPECT_THAT(
      []() {
         Parser parser("x < x <");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:7: Expected Eof but got LessThan")
      )
   );
}

TEST(SaneQLParser, parsesNegativeIntInExpression) {
   Parser parser("age > -1");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "(age > -1)");
}

TEST(SaneQLParser, parsesDoubleNegation) {
   Parser parser("!!(age > -1)");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "(!(!(age > -1)))");
}

TEST(SaneQLParser, parsesDeeplyNestedParens) {
   Parser parser("(((a)))");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "a");
}

TEST(SaneQLParser, parsesMethodCallOnStringLiteral) {
   Parser parser("'hello'.upper()");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "upper('hello')");
}

TEST(SaneQLParser, parsesMethodCallOnParenthesizedExpr) {
   Parser parser("(a || b).filter(true)");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "filter((a || b), true)");
}

TEST(SaneQLParser, parsesFunctionCallWithSingleArg) {
   Parser parser("f(1)");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "f(1)");
}

TEST(SaneQLParser, parsesSetWithSingleElement) {
   // {a} is a set, not a record — no := follows
   Parser parser("{a}");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::SetLiteral>(expr->value));
   EXPECT_EQ(expr->toString(), "{a}");
}

TEST(SaneQLParser, parsesRecordWithSingleField) {
   Parser parser("{x:=1}");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::RecordLiteral>(expr->value));
   EXPECT_EQ(expr->toString(), "{x:=1}");
}

TEST(SaneQLParser, parsesNamedArgWithComplexValue) {
   Parser parser("f(x:=(a && b))");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   const auto& call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(call.function_name, "f");
   ASSERT_EQ(call.named_arguments.size(), 1);
   EXPECT_EQ(call.named_arguments[0].name, "x");
   EXPECT_EQ(expr->toString(), "f(x:=(a && b))");
}

TEST(SaneQLParser, parsesTypeCastOnIntLiteral) {
   Parser parser("42::int");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::TypeCast>(expr->value));
   EXPECT_EQ(expr->toString(), "42::int");
}

TEST(SaneQLParser, parsesWhitespaceOnlyInputAsError) {
   EXPECT_THAT(
      []() {
         Parser parser("   ");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Unexpected token Eof"))
   );
}

TEST(SaneQLParser, throwsOnBareDot) {
   EXPECT_THAT(
      []() {
         Parser parser(".foo");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Unexpected token Dot"))
   );
}

TEST(SaneQLParser, throwsOnDoubleDot) {
   EXPECT_THAT(
      []() {
         Parser parser("a..b");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected Identifier but got Dot"))
   );
}

TEST(SaneQLParser, throwsOnTrailingDot) {
   EXPECT_THAT(
      []() {
         Parser parser("a.");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected Identifier but got Eof"))
   );
}

TEST(SaneQLParser, throwsOnTrailingDoubleColon) {
   EXPECT_THAT(
      []() {
         Parser parser("a::");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected Identifier but got Eof"))
   );
}

TEST(SaneQLParser, throwsOnUnclosedParen) {
   EXPECT_THAT(
      []() {
         Parser parser("(a");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected RightParen but got Eof"))
   );
}

TEST(SaneQLParser, throwsOnDoubleCommaInArgs) {
   EXPECT_THAT(
      []() {
         Parser parser("f(a,,b)");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Unexpected token Comma"))
   );
}

TEST(SaneQLParser, throwsOnLeadingCommaInArgs) {
   EXPECT_THAT(
      []() {
         Parser parser("f(,a)");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Unexpected token Comma"))
   );
}

TEST(SaneQLParser, throwsOnRecordWithTrailingComma) {
   EXPECT_THAT(
      []() {
         Parser parser("{x:=1,}");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected Identifier but got RightBrace"))
   );
}

TEST(SaneQLParser, acceptsDuplicateNamedArgs) {
   // The parser accepts duplicate named args silently; the function registry rejects them
   Parser parser("f(x:=1, x:=2)");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   const auto& call = std::get<ast::FunctionCall>(expr->value);
   ASSERT_EQ(call.named_arguments.size(), 2);
   EXPECT_EQ(call.named_arguments[0].name, "x");
   EXPECT_EQ(call.named_arguments[1].name, "x");
}

TEST(SaneQLParser, parsesChainedOr) {
   Parser parser("a || b || c");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "((a || b) || c)");
}

TEST(SaneQLParser, parsesChainedAnd) {
   Parser parser("a && b && c");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "((a && b) && c)");
}

TEST(SaneQLParser, parsesLessEqual) {
   Parser parser("age <= 30");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::BinaryExpr>(expr->value));
   EXPECT_EQ(std::get<ast::BinaryExpr>(expr->value).op, ast::BinaryOp::LESS_EQUAL);
   EXPECT_EQ(expr->toString(), "(age <= 30)");
}

TEST(SaneQLParser, parsesGreaterEqual) {
   Parser parser("age >= 18");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::BinaryExpr>(expr->value));
   EXPECT_EQ(std::get<ast::BinaryExpr>(expr->value).op, ast::BinaryOp::GREATER_EQUAL);
   EXPECT_EQ(expr->toString(), "(age >= 18)");
}

TEST(SaneQLParser, parsesPropertyAccessWithoutParens) {
   // a.b without () is a property access desugared to a function call with receiver as sole arg
   Parser parser("a.b");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   const auto& call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(call.function_name, "b");
   ASSERT_EQ(call.positional_arguments.size(), 1);
   EXPECT_EQ(call.positional_arguments[0].value->toString(), "a");
   ASSERT_TRUE(call.named_arguments.empty());
   EXPECT_EQ(expr->toString(), "b(a)");
}

TEST(SaneQLParser, parsesMixedPositionalAndNamedArgs) {
   Parser parser("f(1, x:=2)");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   const auto& call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(call.function_name, "f");
   ASSERT_EQ(call.positional_arguments.size(), 1);
   ASSERT_EQ(call.named_arguments.size(), 1);
   EXPECT_EQ(call.named_arguments[0].name, "x");
   EXPECT_EQ(expr->toString(), "f(1, x:=2)");
}

TEST(SaneQLParser, throwsOnUnclosedSet) {
   EXPECT_THAT(
      []() {
         Parser parser("{a, b");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected RightBrace but got Eof"))
   );
}

TEST(SaneQLParser, throwsOnNonIdentifierSecondRecordField) {
   EXPECT_THAT(
      []() {
         Parser parser("{a:=1, 42:=2}");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected Identifier but got IntLiteral"))
   );
}

TEST(SaneQLParser, throwsOnMissingColonEqualsInSecondRecordField) {
   EXPECT_THAT(
      []() {
         Parser parser("{a:=1, b 2}");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected ColonEquals but got IntLiteral"))
   );
}

TEST(SaneQLParser, throwsOnBareAndOperator) {
   EXPECT_THAT(
      []() {
         Parser parser("&&");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Unexpected token And"))
   );
}

TEST(SaneQLParser, throwsPositionalAfterNamed) {
   EXPECT_THAT(
      []() {
         Parser parser("f(x:=1, 2)");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr(
         "Parse error at 1:9: positional argument after named argument is not allowed"
      ))
   );
}

TEST(SaneQLParser, parsesFalseLiteral) {
   Parser parser("false");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::BoolLiteral>(expr->value));
   EXPECT_FALSE(std::get<ast::BoolLiteral>(expr->value).value);
   EXPECT_EQ(expr->toString(), "false");
}

TEST(SaneQLParser, throwsOnMinusWithoutNumber) {
   EXPECT_THAT(
      []() {
         Parser parser("-'hello'");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected number after '-'"))
   );
}

TEST(SaneQLParser, throwsExpressionWithMinus) {
   EXPECT_THAT(
      []() {
         Parser parser("x - 5");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected Eof but got Minus"))
   );
}

TEST(SaneQLParser, parsesInt64Max) {
   Parser parser("9223372036854775807");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::IntLiteral>(expr->value));
   EXPECT_EQ(std::get<ast::IntLiteral>(expr->value).value, INT64_MAX);
}

TEST(SaneQLParser, throwsOnInt64MaxPlusOne) {
   EXPECT_THAT(
      []() {
         Parser parser("9223372036854775808");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Integer literal out of range"))
   );
}

TEST(SaneQLParser, parsesNegativeInt64Max) {
   Parser parser("-9223372036854775807");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::IntLiteral>(expr->value));
   EXPECT_EQ(std::get<ast::IntLiteral>(expr->value).value, -INT64_MAX);
}

TEST(SaneQLParser, parsesInt64Min) {
   Parser parser("-9223372036854775808");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::IntLiteral>(expr->value));
   EXPECT_EQ(std::get<ast::IntLiteral>(expr->value).value, INT64_MIN);
}

TEST(SaneQLParser, throwsOnNegativeInt64MinMinusOne) {
   EXPECT_THAT(
      []() {
         Parser parser("-9223372036854775809");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Integer literal out of range"))
   );
}

TEST(SaneQLParser, parsesTypeCastChaining) {
   Parser parser("a::t1::t2");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::TypeCast>(expr->value));
   const auto& outer = std::get<ast::TypeCast>(expr->value);
   EXPECT_EQ(outer.target_type, "t2");
   ASSERT_TRUE(std::holds_alternative<ast::TypeCast>(outer.operand->value));
   EXPECT_EQ(std::get<ast::TypeCast>(outer.operand->value).target_type, "t1");
   EXPECT_EQ(expr->toString(), "a::t1::t2");
}

TEST(SaneQLParser, parsesBracketIndexAsAtCall) {
   // a[3] is sugar for a.at(3): desugared to at(a, 3)
   Parser parser("a[3]");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   const auto& call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(call.function_name, "at");
   ASSERT_EQ(call.positional_arguments.size(), 2);
   EXPECT_EQ(call.positional_arguments[0].value->toString(), "a");
   EXPECT_EQ(call.positional_arguments[1].value->toString(), "3");
   ASSERT_TRUE(call.named_arguments.empty());
   EXPECT_EQ(expr->toString(), "at(a, 3)");
}

TEST(SaneQLParser, parsesBracketIndexEqualsDotAt) {
   // a[3] and a.at(3) produce identical ASTs
   Parser bracket_parser("a[3]");
   Parser dot_parser("a.at(3)");
   EXPECT_EQ(bracket_parser.parse()->toString(), dot_parser.parse()->toString());
}

TEST(SaneQLParser, parsesChainedBracketIndex) {
   // a[1][2] desugars to at(at(a, 1), 2)
   Parser parser("a[1][2]");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "at(at(a, 1), 2)");
}

TEST(SaneQLParser, throwsOnEmptyBracket) {
   EXPECT_THAT(
      []() {
         Parser parser("a[]");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Unexpected token RightBracket"))
   );
}

TEST(SaneQLParser, throwsOnMultipleIndices) {
   EXPECT_THAT(
      []() {
         Parser parser("a[3, 4]");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected RightBracket but got Comma"))
   );
}

TEST(SaneQLParser, parsesBracketIndexOnMethodCall) {
   // Bracket indexing chains with method calls and other postfix ops
   Parser parser("a.b()[0]");
   auto expr = parser.parse();
   EXPECT_EQ(expr->toString(), "at(b(a), 0)");
}

TEST(SaneQLParser, throwsOnUnclosedBracket) {
   EXPECT_THAT(
      []() {
         Parser parser("a[3");
         (void)parser.parse();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Expected RightBracket but got Eof"))
   );
}

TEST(SaneQLParser, parsesNamedArgInMethodCall) {
   Parser parser("a.f(x:=1)");
   auto expr = parser.parse();
   ASSERT_TRUE(std::holds_alternative<ast::FunctionCall>(expr->value));
   const auto& call = std::get<ast::FunctionCall>(expr->value);
   EXPECT_EQ(call.function_name, "f");
   ASSERT_EQ(call.positional_arguments.size(), 1);
   ASSERT_EQ(call.named_arguments.size(), 1);
   EXPECT_EQ(call.named_arguments[0].name, "x");
   EXPECT_EQ(expr->toString(), "f(a, x:=1)");
}
