#include "rhydb/query_engine/saneql/parser.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>
#include <variant>
#include <vector>

#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/parse_exception.h"

namespace rhydb::query_engine::saneql {

Parser::Parser(std::string_view input)
    : lexer(input),
      current_token(lexer.nextToken()) {}

void Parser::checkNesting(uint32_t depth) const {
   if (nesting_depth + depth > MAX_NESTING_DEPTH) {
      throw ParseException(
         current().location,
         "Expression is nested too deeply, the maximum nesting depth is {}",
         MAX_NESTING_DEPTH
      );
   }
}

ast::ExpressionPtr Parser::makeNode(ast::ExpressionVariant value, SourceLocation location) const {
   auto expr = ast::makeExpr(std::move(value), location);
   checkNesting(expr->depth);
   return expr;
}

Parser::RecursionGuard::RecursionGuard(Parser& parser)
    : parser(parser) {
   ++parser.nesting_depth;
   // The nested expression is at least a leaf
   parser.checkNesting(1);
}

void Parser::advance() {
   current_token = lexer.nextToken();
}

const Token& Parser::current() const {
   return current_token;
}

Token Parser::expect(TokenType type) {
   if (current_token.type != type) {
      throw ParseException(
         current_token.location,
         "Expected {} but got {}",
         tokenTypeToString(type),
         tokenTypeToString(current_token.type)
      );
   }
   Token token = current_token;
   advance();
   return token;
}

bool Parser::check(TokenType type) const {
   return current_token.type == type;
}

bool Parser::match(TokenType type) {
   if (check(type)) {
      advance();
      return true;
   }
   return false;
}

ast::ExpressionPtr Parser::parse() {
   nesting_depth = 0;
   // The top-level expression is not nested in anything
   auto expr = parseOrExpr();
   expect(TokenType::END_OF_FILE);
   return expr;
}

ast::Program Parser::parseProgram() {
   ast::Program program;
   while (check(TokenType::LET)) {
      program.definitions.push_back(parseFunctionDefinition());
   }
   program.body = parseExpression();
   expect(TokenType::END_OF_FILE);
   return program;
}

std::vector<ast::FunctionDefinition> Parser::parseDefinitions() {
   std::vector<ast::FunctionDefinition> definitions;
   while (!check(TokenType::END_OF_FILE)) {
      definitions.push_back(parseFunctionDefinition());
   }
   return definitions;
}

// `let name(parameter, ...) := body,` or `let name := body,`, where a parameter is
// `name type [:= default]`. This is the syntax of SaneQL's let, except that a scalar parameter
// must be declared with the type `scalar` for now.
ast::FunctionDefinition Parser::parseFunctionDefinition() {
   const SourceLocation loc = current().location;
   expect(TokenType::LET);
   ast::FunctionDefinition definition{
      .name = expect(TokenType::IDENTIFIER).getStringValue(),
      .parameters = {},
      .body = nullptr,
      .location = loc,
   };
   if (match(TokenType::LEFT_PAREN)) {
      do {
         const SourceLocation parameter_location = current().location;
         std::string name = expect(TokenType::IDENTIFIER).getStringValue();
         if (!check(TokenType::IDENTIFIER)) {
            throw ParseException(
               current().location,
               "Expected the type of parameter '{}': 'scalar', 'table', 'expression' or 'symbol'",
               name
            );
         }
         const std::string type_name = current().getStringValue();
         const auto type = ast::parameterTypeFromName(type_name);
         if (!type.has_value()) {
            throw ParseException(
               current().location,
               "Unknown type '{}' of parameter '{}'; the types are 'scalar', 'table', 'expression' "
               "and 'symbol'",
               type_name,
               name
            );
         }
         advance();
         ast::DefinitionParameter parameter{
            .name = std::move(name),
            .type = type.value(),
            .default_value = nullptr,
            .location = parameter_location,
         };
         if (match(TokenType::COLON_EQUALS)) {
            parameter.default_value = parseExpression();
         }
         definition.parameters.push_back(std::move(parameter));
      } while (match(TokenType::COMMA));
      expect(TokenType::RIGHT_PAREN);
   }
   expect(TokenType::COLON_EQUALS);
   definition.body = parseExpression();
   expect(TokenType::COMMA);
   return definition;
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseExpression() {
   const RecursionGuard guard{*this};
   return parseOrExpr();
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseOrExpr() {
   return parseLogicalChain(ast::LogicalOp::OR, TokenType::OR, &Parser::parseAndExpr);
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseAndExpr() {
   return parseLogicalChain(ast::LogicalOp::AND, TokenType::AND, &Parser::parseNotExpr);
}

/// Parses `operand (operator_token operand)*` into a single n-ary LogicalExpr. Operands that are
/// themselves a LogicalExpr of the same `op`, e.g. the parenthesized `(a || b)` in
/// `(a || b) || c`, are collapsed into it.
// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseLogicalChain(
   ast::LogicalOp op,
   TokenType operator_token,
   ast::ExpressionPtr (Parser::*parse_operand)()
) {
   auto first_operand = (this->*parse_operand)();
   if (!check(operator_token)) {
      return first_operand;
   }
   const SourceLocation loc = current().location;

   std::vector<ast::ExpressionPtr> operands;
   auto append_operand = [&](ast::ExpressionPtr operand) {
      auto* nested = std::get_if<ast::LogicalExpr>(&operand->value);
      if (nested != nullptr && nested->op == op) {
         std::ranges::move(nested->operands, std::back_inserter(operands));
      } else {
         operands.push_back(std::move(operand));
      }
   };
   append_operand(std::move(first_operand));
   while (match(operator_token)) {
      append_operand((this->*parse_operand)());
   }

   return makeNode(ast::LogicalExpr{.op = op, .operands = std::move(operands)}, loc);
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseNotExpr() {
   if (check(TokenType::NOT)) {
      const RecursionGuard guard{*this};
      const SourceLocation loc = current().location;
      advance();
      auto operand = parseNotExpr();
      return makeNode(ast::UnaryNotExpr{std::move(operand)}, loc);
   }
   return parseComparisonExpr();
}

std::optional<ast::BinaryOp> Parser::tryParseBinaryOp() {
   ast::BinaryOp op;
   switch (current().type) {
      case TokenType::EQUALS:
         op = ast::BinaryOp::EQUALS;
         break;
      case TokenType::NOT_EQUALS:
         op = ast::BinaryOp::NOT_EQUALS;
         break;
      case TokenType::LESS_THAN:
         op = ast::BinaryOp::LESS_THAN;
         break;
      case TokenType::LESS_EQUAL:
         op = ast::BinaryOp::LESS_EQUAL;
         break;
      case TokenType::GREATER_THAN:
         op = ast::BinaryOp::GREATER_THAN;
         break;
      case TokenType::GREATER_EQUAL:
         op = ast::BinaryOp::GREATER_EQUAL;
         break;
      default:
         return std::nullopt;
   }
   advance();
   return op;
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseComparisonExpr() {
   auto left = parsePostfixExpr();

   const SourceLocation loc = current().location;
   if (auto comparator = tryParseBinaryOp()) {
      auto right = parsePostfixExpr();
      left = makeNode(
         ast::BinaryExpr{
            .op = comparator.value(),
            .left = std::move(left),
            .right = std::move(right),
         },
         loc
      );
   }

   return left;
}

/// Parses a primary expression and the postfix operations following it, e.g. `.f(x)[i]::int`.
/// Each operation wraps `expr` in a new node, whose depth makeNode checks.
// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parsePostfixExpr() {
   auto expr = parsePrimaryExpr();
   while (true) {
      if (check(TokenType::DOT)) {
         advance();
         const Token method_name = expect(TokenType::IDENTIFIER);
         if (check(TokenType::LEFT_PAREN)) {
            advance();
            ParsedArgs parsed;
            // Insert the receiver as the first positional argument
            parsed.positional.push_back(
               ast::PositionalArgument{.value = std::move(expr), .location = method_name.location}
            );
            if (!check(TokenType::RIGHT_PAREN)) {
               auto rest = parseArgList();
               parsed.positional.insert(
                  parsed.positional.end(),
                  std::make_move_iterator(rest.positional.begin()),
                  std::make_move_iterator(rest.positional.end())
               );
               parsed.named = std::move(rest.named);
            }
            expect(TokenType::RIGHT_PAREN);
            expr = makeNode(
               ast::FunctionCall{
                  .function_name = method_name.getStringValue(),
                  .positional_arguments = std::move(parsed.positional),
                  .named_arguments = std::move(parsed.named),
               },
               method_name.location
            );
         } else {
            // Property access — treat as function call with receiver as sole arg
            std::vector<ast::PositionalArgument> pos_args;
            pos_args.push_back(
               ast::PositionalArgument{.value = std::move(expr), .location = method_name.location}
            );
            expr = makeNode(
               ast::FunctionCall{
                  .function_name = method_name.getStringValue(),
                  .positional_arguments = std::move(pos_args),
                  .named_arguments = {},
               },
               method_name.location
            );
         }
      } else if (check(TokenType::LEFT_BRACKET)) {
         const SourceLocation loc = current().location;
         advance();
         auto index = parseExpression();
         expect(TokenType::RIGHT_BRACKET);
         // Desugar a[i] to at(a, i): receiver becomes first positional argument
         std::vector<ast::PositionalArgument> pos_args;
         pos_args.push_back(ast::PositionalArgument{.value = std::move(expr), .location = loc});
         pos_args.push_back(ast::PositionalArgument{.value = std::move(index), .location = loc});
         expr = makeNode(
            ast::FunctionCall{
               .function_name = "at",
               .positional_arguments = std::move(pos_args),
               .named_arguments = {},
            },
            loc
         );
      } else if (check(TokenType::DOUBLE_COLON)) {
         const SourceLocation loc = current().location;
         advance();
         const Token type_name = expect(TokenType::IDENTIFIER);
         expr = makeNode(
            ast::TypeCast{.operand = std::move(expr), .target_type = type_name.getStringValue()},
            loc
         );
      } else {
         return expr;
      }
   }
}

ast::ExpressionPtr Parser::parseUnaryMinus() {
   const SourceLocation loc = current().location;
   expect(TokenType::MINUS);

   if (check(TokenType::INT_LITERAL)) {
      const uint64_t val = current().getIntValue();
      advance();
      constexpr auto INT64_MAX_AS_UINT = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
      if (val <= INT64_MAX_AS_UINT) {
         return makeNode(ast::IntLiteral{-static_cast<int64_t>(val)}, loc);
      }
      if (val == INT64_MAX_AS_UINT + 1) {
         return makeNode(ast::IntLiteral{std::numeric_limits<int64_t>::min()}, loc);
      }
      throw ParseException(loc, "Integer literal out of range");
   }

   if (check(TokenType::FLOAT_LITERAL)) {
      const double val = current().getFloatValue();
      advance();
      return makeNode(ast::FloatLiteral{-val}, loc);
   }

   throw ParseException(loc, "Expected number after '-'");
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parsePrimaryExpr() {
   if (check(TokenType::LEFT_PAREN)) {
      advance();
      auto expr = parseExpression();
      expect(TokenType::RIGHT_PAREN);
      return expr;
   }

   if (check(TokenType::LEFT_BRACE)) {
      return parseSetOrRecordExpression();
   }

   if (check(TokenType::MINUS)) {
      return parseUnaryMinus();
   }

   if (check(TokenType::IDENTIFIER)) {
      return parseIdentifierOrFunctionCall();
   }

   return parseLiteral();
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseSetOrRecordExpression() {
   const SourceLocation loc = current().location;
   expect(TokenType::LEFT_BRACE);
   // Empty braces: empty set
   if (check(TokenType::RIGHT_BRACE)) {
      advance();
      return makeNode(ast::SetLiteral{{}}, loc);
   }

   auto first_expression = parseExpression();
   // Peek: if next element is `:=`, parse as RecordLiteral
   if (check(TokenType::COLON_EQUALS)) {
      // RecordLiteral: {name := expr, ...}
      if (!holds_alternative<ast::Identifier>(first_expression->value)) {
         throw ParseException(
            loc, "A RecordLiteral requires a simple identifier expression as field names"
         );
      }
      advance();
      std::vector<ast::RecordField> fields;
      auto first_value = parseExpression();
      fields.push_back({.name = first_expression->toString(), .value = std::move(first_value)});
      while (match(TokenType::COMMA)) {
         const Token field_name = expect(TokenType::IDENTIFIER);
         expect(TokenType::COLON_EQUALS);
         auto value = parseExpression();
         fields.push_back({.name = field_name.getStringValue(), .value = std::move(value)});
      }
      expect(TokenType::RIGHT_BRACE);
      return makeNode(ast::RecordLiteral{std::move(fields)}, loc);
   }
   std::vector<ast::ExpressionPtr> elements;
   elements.push_back(std::move(first_expression));
   while (match(TokenType::COMMA)) {
      elements.push_back(parseExpression());
   }
   expect(TokenType::RIGHT_BRACE);
   return makeNode(ast::SetLiteral{std::move(elements)}, loc);
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseIdentifierOrFunctionCall() {
   const SourceLocation loc = current().location;
   std::string name = current().getStringValue();
   advance();

   if (check(TokenType::LEFT_PAREN)) {
      advance();
      ParsedArgs parsed;
      if (!check(TokenType::RIGHT_PAREN)) {
         parsed = parseArgList();
      }
      expect(TokenType::RIGHT_PAREN);
      return makeNode(
         ast::FunctionCall{
            .function_name = std::move(name),
            .positional_arguments = std::move(parsed.positional),
            .named_arguments = std::move(parsed.named),
         },
         loc
      );
   }

   return makeNode(ast::Identifier{std::move(name)}, loc);
}

ast::ExpressionPtr Parser::parseLiteral() {
   const SourceLocation loc = current().location;

   if (check(TokenType::STRING_LITERAL)) {
      std::string val = current().getStringValue();
      advance();
      return makeNode(ast::StringLiteral{std::move(val)}, loc);
   }

   if (check(TokenType::INT_LITERAL)) {
      const uint64_t val = current().getIntValue();
      if (val > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
         throw ParseException(loc, "Integer literal out of range");
      }
      advance();
      return makeNode(ast::IntLiteral{static_cast<int64_t>(val)}, loc);
   }

   if (check(TokenType::FLOAT_LITERAL)) {
      const double val = current().getFloatValue();
      advance();
      return makeNode(ast::FloatLiteral{val}, loc);
   }

   if (check(TokenType::BOOL_LITERAL)) {
      const bool val = current().getBoolValue();
      advance();
      return makeNode(ast::BoolLiteral{val}, loc);
   }

   if (check(TokenType::NULL_LITERAL)) {
      advance();
      return makeNode(ast::NullLiteral{}, loc);
   }

   throw ParseException(loc, "Unexpected token {}", tokenTypeToString(current().type));
}

// NOLINTNEXTLINE(misc-no-recursion)
Parser::ParsedArgs Parser::parseArgList() {
   ParsedArgs result;
   bool seen_named = false;

   // NOLINTNEXTLINE(misc-no-recursion)
   auto parse_one = [&] {
      const SourceLocation loc = current().location;
      auto expr = parseExpression();
      if (check(TokenType::COLON_EQUALS)) {
         // we are a named argument -> the first expression must have been a simple identifier
         if (!holds_alternative<ast::Identifier>(expr->value)) {
            throw ParseException(
               loc,
               "A named argument requires a simple identifier as the name, but got '{}'",
               expr->toString()
            );
         }
         advance();
         auto value = parseExpression();
         seen_named = true;
         result.named.push_back(
            ast::NamedArgument{.name = expr->toString(), .value = std::move(value), .location = loc}
         );
         return;
      }
      if (seen_named) {
         throw ParseException(loc, "positional argument after named argument is not allowed");
      }
      result.positional.push_back(
         ast::PositionalArgument{.value = std::move(expr), .location = loc}
      );
      return;
   };

   parse_one();
   while (match(TokenType::COMMA)) {
      parse_one();
   }
   return result;
}

}  // namespace rhydb::query_engine::saneql
