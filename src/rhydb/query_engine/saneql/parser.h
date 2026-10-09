#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/lexer.h"
#include "rhydb/query_engine/saneql/token.h"

namespace rhydb::query_engine::saneql {

class Parser {
  public:
   /// Every expression together with the levels it is nested in may be at most this deep, see
   /// checkNesting. This bounds the depth of the AST, along which all later passes (rewriting,
   /// compilation, destruction) recurse, and the recursion of the parser itself.
   static constexpr size_t MAX_NESTING_DEPTH = 100;

  private:
   Lexer lexer;
   Token current_token;
   /// The number of levels the parser is nested in at the current position: parentheses, argument
   /// lists, sets, records, indices and `!`
   size_t nesting_depth = 0;

   /// Throws a ParseException if an expression of `depth` (see ast::Expression::depth) does not fit
   /// within MAX_NESTING_DEPTH together with the nesting_depth levels it is nested in
   void checkNesting(uint32_t depth) const;

   /// ast::makeExpr, followed by checkNesting for the new expression
   [[nodiscard]] ast::ExpressionPtr makeNode(
      ast::ExpressionVariant value,
      SourceLocation location
   ) const;

   /// Put at the top of every recursive parse function: counts one level of nesting while it is
   /// alive and calls checkNesting for the nested expression.
   class RecursionGuard {
      Parser& parser;

     public:
      explicit RecursionGuard(Parser& parser);
      RecursionGuard(const RecursionGuard&) = delete;
      RecursionGuard& operator=(const RecursionGuard&) = delete;
      RecursionGuard(RecursionGuard&&) = delete;
      RecursionGuard& operator=(RecursionGuard&&) = delete;
      ~RecursionGuard() { --parser.nesting_depth; }
   };

  public:
   explicit Parser(std::string_view input);

   /// Parses a single expression.
   [[nodiscard]] ast::ExpressionPtr parse();

   /// Parses a query: any number of `let` definitions, each followed by a comma, and then an
   /// expression.
   [[nodiscard]] ast::Program parseProgram();

   /// Parses a sequence of `let` definitions, each followed by a comma, and nothing else.
   [[nodiscard]] std::vector<ast::FunctionDefinition> parseDefinitions();

  private:
   void advance();
   [[nodiscard]] const Token& current() const;
   Token expect(TokenType type);
   [[nodiscard]] bool check(TokenType type) const;
   [[nodiscard]] bool match(TokenType type);

   [[nodiscard]] ast::FunctionDefinition parseFunctionDefinition();
   [[nodiscard]] ast::ExpressionPtr parseExpression();
   [[nodiscard]] ast::ExpressionPtr parseOrExpr();
   [[nodiscard]] ast::ExpressionPtr parseAndExpr();
   [[nodiscard]] ast::ExpressionPtr parseLogicalChain(
      ast::LogicalOp op,
      TokenType operator_token,
      ast::ExpressionPtr (Parser::*parse_operand)()
   );
   [[nodiscard]] ast::ExpressionPtr parseNotExpr();
   [[nodiscard]] std::optional<ast::BinaryOp> tryParseBinaryOp();
   [[nodiscard]] ast::ExpressionPtr parseComparisonExpr();
   [[nodiscard]] ast::ExpressionPtr parsePostfixExpr();
   [[nodiscard]] ast::ExpressionPtr parsePrimaryExpr();
   [[nodiscard]] ast::ExpressionPtr parseUnaryMinus();
   [[nodiscard]] ast::ExpressionPtr parseSetOrRecordExpression();
   [[nodiscard]] ast::ExpressionPtr parseIdentifierOrFunctionCall();
   [[nodiscard]] ast::ExpressionPtr parseLiteral();

   struct ParsedArgs {
      std::vector<ast::PositionalArgument> positional;
      std::vector<ast::NamedArgument> named;
   };
   [[nodiscard]] ParsedArgs parseArgList();
};

}  // namespace rhydb::query_engine::saneql
