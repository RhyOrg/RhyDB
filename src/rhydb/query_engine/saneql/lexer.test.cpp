#include "rhydb/query_engine/saneql/lexer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/saneql/parse_exception.h"

using rhydb::query_engine::saneql::Lexer;
using rhydb::query_engine::saneql::ParseException;
using rhydb::query_engine::saneql::TokenType;

TEST(SaneQLLexer, tokenizesEmptyInput) {
   Lexer lexer("");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 1);
   EXPECT_EQ(tokens[0].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, tokenizesIdentifier) {
   Lexer lexer("country");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "country");
   EXPECT_EQ(tokens[1].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, tokenizesStringLiteral) {
   Lexer lexer("'hello world'");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[0].getStringValue(), "hello world");
}

TEST(SaneQLLexer, tokenizesIntLiteral) {
   Lexer lexer("42");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[0].getIntValue(), 42);
}

TEST(SaneQLLexer, invalidIntLiteral) {
   EXPECT_THAT(
      []() {
         const std::string input = fmt::format("{}0", UINT64_MAX);
         Lexer lexer(input);
         auto tokens = lexer.tokenizeAll();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Invalid integer literal")
      )
   );
}

TEST(SaneQLLexer, tokenizesFloatLiteral) {
   Lexer lexer("3.14");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::FLOAT_LITERAL);
   EXPECT_DOUBLE_EQ(tokens[0].getFloatValue(), 3.14);
}

TEST(SaneQLLexer, invalidFloatLiteral) {
   EXPECT_THAT(
      []() {
         Lexer lexer("1.2.3");
         auto tokens = lexer.tokenizeAll();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Invalid float literal")
      )
   );
}

TEST(SaneQLLexer, tokenizesBoolLiterals) {
   Lexer lexer("true false");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::BOOL_LITERAL);
   EXPECT_TRUE(tokens[0].getBoolValue());
   EXPECT_EQ(tokens[1].type, TokenType::BOOL_LITERAL);
   EXPECT_FALSE(tokens[1].getBoolValue());
}

TEST(SaneQLLexer, tokenizesNullLiteral) {
   Lexer lexer("null");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::NULL_LITERAL);
}

TEST(SaneQLLexer, tokenizesOperators) {
   Lexer lexer("= <> < > <= >= && || ! -");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 11);
   EXPECT_EQ(tokens[0].type, TokenType::EQUALS);
   EXPECT_EQ(tokens[1].type, TokenType::NOT_EQUALS);
   EXPECT_EQ(tokens[2].type, TokenType::LESS_THAN);
   EXPECT_EQ(tokens[3].type, TokenType::GREATER_THAN);
   EXPECT_EQ(tokens[4].type, TokenType::LESS_EQUAL);
   EXPECT_EQ(tokens[5].type, TokenType::GREATER_EQUAL);
   EXPECT_EQ(tokens[6].type, TokenType::AND);
   EXPECT_EQ(tokens[7].type, TokenType::OR);
   EXPECT_EQ(tokens[8].type, TokenType::NOT);
   EXPECT_EQ(tokens[9].type, TokenType::MINUS);
}

TEST(SaneQLLexer, invalidPartialAnd) {
   EXPECT_THAT(
      []() {
         Lexer lexer("a & b");
         auto tokens = lexer.tokenizeAll();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Parse error at 1:3: Expected '&&'"))
   );
}

TEST(SaneQLLexer, invalidPartialOr) {
   EXPECT_THAT(
      []() {
         Lexer lexer("a | b");
         auto tokens = lexer.tokenizeAll();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Parse error at 1:3: Expected '||'"))
   );
}

TEST(SaneQLLexer, invalidPartialColon) {
   EXPECT_THAT(
      []() {
         Lexer lexer("a : b");
         auto tokens = lexer.tokenizeAll();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:3: Expected '::' or ':='")
      )
   );
}

TEST(SaneQLLexer, tokenizesPunctuation) {
   Lexer lexer(". , ( ) { } :: :=");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 9);
   EXPECT_EQ(tokens[0].type, TokenType::DOT);
   EXPECT_EQ(tokens[1].type, TokenType::COMMA);
   EXPECT_EQ(tokens[2].type, TokenType::LEFT_PAREN);
   EXPECT_EQ(tokens[3].type, TokenType::RIGHT_PAREN);
   EXPECT_EQ(tokens[4].type, TokenType::LEFT_BRACE);
   EXPECT_EQ(tokens[5].type, TokenType::RIGHT_BRACE);
   EXPECT_EQ(tokens[6].type, TokenType::DOUBLE_COLON);
   EXPECT_EQ(tokens[7].type, TokenType::COLON_EQUALS);
}

TEST(SaneQLLexer, tokenizesBrackets) {
   Lexer lexer("[ ]");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::LEFT_BRACKET);
   EXPECT_EQ(tokens[1].type, TokenType::RIGHT_BRACKET);
   EXPECT_EQ(tokens[2].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, tokenizesBracketIndexing) {
   Lexer lexer("a[3]");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 5);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "a");
   EXPECT_EQ(tokens[1].type, TokenType::LEFT_BRACKET);
   EXPECT_EQ(tokens[2].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[2].getIntValue(), 3);
   EXPECT_EQ(tokens[3].type, TokenType::RIGHT_BRACKET);
   EXPECT_EQ(tokens[4].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, tokenizesMethodCallChain) {
   Lexer lexer("data.filter(country = 'USA').group(by:={}, aggs:={count:=count()})");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 27);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "data");
   EXPECT_EQ(tokens[1].type, TokenType::DOT);
   EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[2].getStringValue(), "filter");
   EXPECT_EQ(tokens[3].type, TokenType::LEFT_PAREN);
   EXPECT_EQ(tokens[4].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[4].getStringValue(), "country");
   EXPECT_EQ(tokens[5].type, TokenType::EQUALS);
   EXPECT_EQ(tokens[6].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[6].getStringValue(), "USA");
   EXPECT_EQ(tokens[7].type, TokenType::RIGHT_PAREN);
   EXPECT_EQ(tokens[8].type, TokenType::DOT);
   EXPECT_EQ(tokens[9].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[9].getStringValue(), "group");
   EXPECT_EQ(tokens[10].type, TokenType::LEFT_PAREN);
   EXPECT_EQ(tokens[11].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[11].getStringValue(), "by");
   EXPECT_EQ(tokens[12].type, TokenType::COLON_EQUALS);
   EXPECT_EQ(tokens[13].type, TokenType::LEFT_BRACE);
   EXPECT_EQ(tokens[14].type, TokenType::RIGHT_BRACE);
   EXPECT_EQ(tokens[15].type, TokenType::COMMA);
   EXPECT_EQ(tokens[16].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[16].getStringValue(), "aggs");
   EXPECT_EQ(tokens[17].type, TokenType::COLON_EQUALS);
   EXPECT_EQ(tokens[18].type, TokenType::LEFT_BRACE);
   EXPECT_EQ(tokens[19].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[20].type, TokenType::COLON_EQUALS);
   EXPECT_EQ(tokens[21].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[22].type, TokenType::LEFT_PAREN);
   EXPECT_EQ(tokens[23].type, TokenType::RIGHT_PAREN);
   EXPECT_EQ(tokens[24].type, TokenType::RIGHT_BRACE);
   EXPECT_EQ(tokens[25].type, TokenType::RIGHT_PAREN);
   EXPECT_EQ(tokens[26].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, tokenizesNamedParameters) {
   Lexer lexer("hasMutation(position:=1000, sequenceName:='segment1')");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 11);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].type, TokenType::LEFT_PAREN);
   EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[2].getStringValue(), "position");
   EXPECT_EQ(tokens[3].type, TokenType::COLON_EQUALS);
   EXPECT_EQ(tokens[4].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[5].type, TokenType::COMMA);
   EXPECT_EQ(tokens[6].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[7].type, TokenType::COLON_EQUALS);
   EXPECT_EQ(tokens[8].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[9].type, TokenType::RIGHT_PAREN);
}

TEST(SaneQLLexer, tokenizesTypeCast) {
   Lexer lexer("'2020-01-01'::date");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 4);
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[1].type, TokenType::DOUBLE_COLON);
   EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[2].getStringValue(), "date");
}

TEST(SaneQLLexer, skipsWhitespaceAndNewlines) {
   Lexer lexer("  metadata\n  .filter(\n    country = 'USA'\n  )");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 9);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].type, TokenType::DOT);
}

TEST(SaneQLLexer, tracksLineAndColumn) {
   Lexer lexer("a\nb");
   auto tokens = lexer.tokenizeAll();
   EXPECT_EQ(tokens[0].location.line, 1);
   EXPECT_EQ(tokens[0].location.column, 1);
   EXPECT_EQ(tokens[1].location.line, 2);
   EXPECT_EQ(tokens[1].location.column, 1);
}

TEST(SaneQLLexer, throwsOnUnterminatedString) {
   EXPECT_THAT(
      []() {
         Lexer lexer("'unterminated");
         (void)lexer.nextToken();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Unterminated string literal")
      )
   );
}

TEST(SaneQLLexer, throwsOnInvalidCharacter) {
   EXPECT_THAT(
      []() {
         Lexer lexer("@");
         (void)lexer.nextToken();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Unexpected character '@'")
      )
   );
}

TEST(SaneQLLexer, skipsLineComments) {
   Lexer lexer("a -- this is a comment\nb");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "a");
   EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].getStringValue(), "b");
}

TEST(SaneQLLexer, tokenizesSetLiteral) {
   Lexer lexer("{'USA', 'Germany', 'France'}");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 8);
   EXPECT_EQ(tokens[0].type, TokenType::LEFT_BRACE);
   EXPECT_EQ(tokens[1].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[1].getStringValue(), "USA");
   EXPECT_EQ(tokens[2].type, TokenType::COMMA);
   EXPECT_EQ(tokens[3].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[4].type, TokenType::COMMA);
   EXPECT_EQ(tokens[5].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[6].type, TokenType::RIGHT_BRACE);
}

TEST(SaneQLLexer, handlesEscapedDoubleQuotesInStrings) {
   Lexer lexer("'it''s'");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[0].getStringValue(), "it's");
}

TEST(SaneQLLexer, handlesEscapedDoubleQuotesInStringsAtEnd) {
   Lexer lexer("'it'''");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[0].getStringValue(), "it'");
}

TEST(SaneQLLexer, tokenizesQuotedIdentifier) {
   Lexer lexer(R"("my column")");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "my column");
}

TEST(SaneQLLexer, quotedIdentifierWithEscapedDoubleQuote) {
   Lexer lexer(R"("say ""hello""")");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), R"(say "hello")");
}

TEST(SaneQLLexer, throwsOnUnterminatedQuotedIdentifier) {
   EXPECT_THAT(
      []() {
         Lexer lexer(R"("unterminated)");
         (void)lexer.nextToken();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Unterminated quoted identifier")
      )
   );
}

TEST(SaneQLLexer, quotedIdentifierWithNumericName) {
   Lexer lexer(R"("2")");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "2");
}

TEST(SaneQLLexer, tokenizesMinusInt) {
   Lexer lexer("-42");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::MINUS);
   EXPECT_EQ(tokens[1].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[1].getIntValue(), 42);
}

TEST(SaneQLLexer, tokenizesMinusFloat) {
   Lexer lexer("-3.14");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::MINUS);
   EXPECT_EQ(tokens[1].type, TokenType::FLOAT_LITERAL);
   EXPECT_DOUBLE_EQ(tokens[1].getFloatValue(), 3.14);
}

TEST(SaneQLLexer, disambiguatesMinusFromNegative) {
   Lexer lexer("x -42");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 4);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "x");
   EXPECT_EQ(tokens[1].type, TokenType::MINUS);
   EXPECT_EQ(tokens[2].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[2].getIntValue(), 42);
}

TEST(SaneQLLexer, tokenizesEmptyStringLiteral) {
   Lexer lexer("''");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[0].getStringValue(), "");
}

TEST(SaneQLLexer, tokenizesIdentifierWithUnderscores) {
   Lexer lexer("some_column");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "some_column");
}

TEST(SaneQLLexer, tokenizesIdentifierStartingWithUnderscore) {
   Lexer lexer("_private");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "_private");
}

TEST(SaneQLLexer, leadingDotIsNotAFloat) {
   // .5 is not a float literal — '.' is a DOT token, '5' is an int
   Lexer lexer(".5");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::DOT);
   EXPECT_EQ(tokens[1].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[1].getIntValue(), 5);
}

TEST(SaneQLLexer, trailingDotIsAFloat) {
   // 5. has a dot so is_float=true; fast_float parses "5." as 5.0
   Lexer lexer("5.");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::FLOAT_LITERAL);
   EXPECT_DOUBLE_EQ(tokens[0].getFloatValue(), 5.0);
}

TEST(SaneQLLexer, skipsCommentAtEndOfInputWithNoNewline) {
   Lexer lexer("a -- comment");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "a");
   EXPECT_EQ(tokens[1].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, skipsTabs) {
   Lexer lexer("a\tb");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "a");
   EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].getStringValue(), "b");
}

TEST(SaneQLLexer, handlesCRLFLineEndings) {
   Lexer lexer("a\r\nb");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].location.line, 1);
   EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].location.line, 2);
}

TEST(SaneQLLexer, partialAndAtEofThrows) {
   EXPECT_THAT(
      []() {
         Lexer lexer("&");
         (void)lexer.nextToken();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Parse error at 1:1: Expected '&&'"))
   );
}

TEST(SaneQLLexer, partialOrAtEofThrows) {
   EXPECT_THAT(
      []() {
         Lexer lexer("|");
         (void)lexer.nextToken();
      },
      ThrowsMessage<ParseException>(::testing::HasSubstr("Parse error at 1:1: Expected '||'"))
   );
}

TEST(SaneQLLexer, partialColonAtEofThrows) {
   EXPECT_THAT(
      []() {
         Lexer lexer(":");
         (void)lexer.nextToken();
      },
      ThrowsMessage<ParseException>(
         ::testing::HasSubstr("Parse error at 1:1: Expected '::' or ':='")
      )
   );
}

TEST(SaneQLLexer, lessThanAtEofProducesLessThan) {
   Lexer lexer("<");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::LESS_THAN);
   EXPECT_EQ(tokens[1].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, greaterThanAtEofProducesGreaterThan) {
   Lexer lexer(">");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::GREATER_THAN);
   EXPECT_EQ(tokens[1].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, quotedIdentifierWithEmbeddedNewline) {
   Lexer lexer("\"col\nname\"");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "col\nname");
}

TEST(SaneQLLexer, whitespaceOnlyProducesEof) {
   Lexer lexer("   ");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 1);
   EXPECT_EQ(tokens[0].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, commentOnlyProducesEof) {
   Lexer lexer("-- foo");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 1);
   EXPECT_EQ(tokens[0].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, emptyQuotedIdentifierProducesEmptyString) {
   Lexer lexer(R"("")");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "");
}

TEST(SaneQLLexer, tokenizesMinusToken) {
   Lexer lexer("-");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::MINUS);
   EXPECT_EQ(tokens[1].type, TokenType::END_OF_FILE);
}

TEST(SaneQLLexer, dashFollowedByIdentifier) {
   Lexer lexer("-x");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 3);
   EXPECT_EQ(tokens[0].type, TokenType::MINUS);
   EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].getStringValue(), "x");
}

TEST(SaneQLLexer, multipleConsecutiveComments) {
   Lexer lexer("-- first\n-- second\na");
   auto tokens = lexer.tokenizeAll();
   ASSERT_EQ(tokens.size(), 2);
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].getStringValue(), "a");
   EXPECT_EQ(tokens[1].type, TokenType::END_OF_FILE);
}
