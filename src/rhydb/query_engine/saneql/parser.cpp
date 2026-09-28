#include "rhydb/query_engine/saneql/parser.h"

#include <cstdint>
#include <limits>

#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/parse_exception.h"

namespace rhydb::query_engine::saneql {

Parser::Parser(std::string_view input)
    : lexer(input),
      current_token(lexer.nextToken()) {}

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
   auto expr = parseExpression();
   expect(TokenType::END_OF_FILE);
   return expr;
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseExpression() {
   return parseOrExpr();
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseOrExpr() {
   auto left = parseAndExpr();

   while (check(TokenType::OR)) {
      const SourceLocation loc = current().location;
      advance();
      auto right = parseAndExpr();
      left = ast::makeExpr(
         ast::BinaryExpr{
            .op = ast::BinaryOp::OR, .left = std::move(left), .right = std::move(right),
         },
         loc
      );
   }

   return left;
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseAndExpr() {
   auto left = parseNotExpr();

   while (check(TokenType::AND)) {
      const SourceLocation loc = current().location;
      advance();
      auto right = parseNotExpr();
      left = ast::makeExpr(
         ast::BinaryExpr{
            .op = ast::BinaryOp::AND, .left = std::move(left), .right = std::move(right),
         },
         loc
      );
   }

   return left;
}

// NOLINTNEXTLINE(misc-no-recursion)
ast::ExpressionPtr Parser::parseNotExpr() {
   if (check(TokenType::NOT)) {
      const SourceLocation loc = current().location;
      advance();
      auto operand = parseNotExpr();
      return ast::makeExpr(ast::UnaryNotExpr{std::move(operand)}, loc);
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
      left = ast::makeExpr(
         ast::BinaryExpr{
            .op = comparator.value(), .left = std::move(left), .right = std::move(right),
         },
         loc
      );
   }

   return left;
}

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
            expr = ast::makeExpr(
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
            expr = ast::makeExpr(
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
         expr = ast::makeExpr(
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
         expr = ast::makeExpr(
            ast::TypeCast{.operand = std::move(expr), .target_type = type_name.getStringValue()},
            loc
         );
      } else {
         break;
      }
   }

   return expr;
}

ast::ExpressionPtr Parser::parseUnaryMinus() {
   const SourceLocation loc = current().location;
   expect(TokenType::MINUS);

   if (check(TokenType::INT_LITERAL)) {
      const uint64_t val = current().getIntValue();
      advance();
      constexpr auto INT64_MAX_AS_UINT = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
      if (val <= INT64_MAX_AS_UINT) {
         return ast::makeExpr(ast::IntLiteral{-static_cast<int64_t>(val)}, loc);
      }
      if (val == INT64_MAX_AS_UINT + 1) {
         return ast::makeExpr(ast::IntLiteral{std::numeric_limits<int64_t>::min()}, loc);
      }
      throw ParseException(loc, "Integer literal out of range");
   }

   if (check(TokenType::FLOAT_LITERAL)) {
      const double val = current().getFloatValue();
      advance();
      return ast::makeExpr(ast::FloatLiteral{-val}, loc);
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
      return ast::makeExpr(ast::SetLiteral{{}}, loc);
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
      fields.push_back({first_expression->toString(), std::move(first_value)});
      while (match(TokenType::COMMA)) {
         const Token field_name = expect(TokenType::IDENTIFIER);
         expect(TokenType::COLON_EQUALS);
         auto value = parseExpression();
         fields.push_back({field_name.getStringValue(), std::move(value)});
      }
      expect(TokenType::RIGHT_BRACE);
      return ast::makeExpr(ast::RecordLiteral{std::move(fields)}, loc);
   }
   std::vector<ast::ExpressionPtr> elements;
   elements.push_back(std::move(first_expression));
   while (match(TokenType::COMMA)) {
      elements.push_back(parseExpression());
   }
   expect(TokenType::RIGHT_BRACE);
   return ast::makeExpr(ast::SetLiteral{std::move(elements)}, loc);
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
      return ast::makeExpr(
         ast::FunctionCall{
            .function_name = std::move(name),
            .positional_arguments = std::move(parsed.positional),
            .named_arguments = std::move(parsed.named),
         },
         loc
      );
   }

   return ast::makeExpr(ast::Identifier{std::move(name)}, loc);
}

ast::ExpressionPtr Parser::parseLiteral() {
   const SourceLocation loc = current().location;

   if (check(TokenType::STRING_LITERAL)) {
      std::string val = current().getStringValue();
      advance();
      return ast::makeExpr(ast::StringLiteral{std::move(val)}, loc);
   }

   if (check(TokenType::INT_LITERAL)) {
      const uint64_t val = current().getIntValue();
      if (val > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
         throw ParseException(loc, "Integer literal out of range");
      }
      advance();
      return ast::makeExpr(ast::IntLiteral{static_cast<int64_t>(val)}, loc);
   }

   if (check(TokenType::FLOAT_LITERAL)) {
      const double val = current().getFloatValue();
      advance();
      return ast::makeExpr(ast::FloatLiteral{val}, loc);
   }

   if (check(TokenType::BOOL_LITERAL)) {
      const bool val = current().getBoolValue();
      advance();
      return ast::makeExpr(ast::BoolLiteral{val}, loc);
   }

   if (check(TokenType::NULL_LITERAL)) {
      advance();
      return ast::makeExpr(ast::NullLiteral{}, loc);
   }

   throw ParseException(loc, "Unexpected token {}", tokenTypeToString(current().type));
}

// NOLINTNEXTLINE(misc-no-recursion)
Parser::ParsedArgs Parser::parseArgList() {
   ParsedArgs result;
   bool seen_named = false;

   // NOLINTNEXTLINE(misc-no-recursion)
   auto parse_one = [&]() {
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
      result.positional.push_back(ast::PositionalArgument{.value = std::move(expr), .location = loc}
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
