#include "rhydb/query_engine/saneql/token.h"

#include <fmt/format.h>

namespace rhydb::query_engine::saneql {

std::string tokenTypeToString(TokenType type) {
   switch (type) {
      case TokenType::INT_LITERAL:
         return "IntLiteral";
      case TokenType::FLOAT_LITERAL:
         return "FloatLiteral";
      case TokenType::STRING_LITERAL:
         return "StringLiteral";
      case TokenType::BOOL_LITERAL:
         return "BoolLiteral";
      case TokenType::NULL_LITERAL:
         return "NullLiteral";
      case TokenType::IDENTIFIER:
         return "Identifier";
      case TokenType::LET:
         return "Let";
      case TokenType::DOT:
         return "Dot";
      case TokenType::DOUBLE_COLON:
         return "DoubleColon";
      case TokenType::COLON_EQUALS:
         return "ColonEquals";
      case TokenType::EQUALS:
         return "Equals";
      case TokenType::NOT_EQUALS:
         return "NotEquals";
      case TokenType::LESS_THAN:
         return "LessThan";
      case TokenType::LESS_EQUAL:
         return "LessEqual";
      case TokenType::GREATER_THAN:
         return "GreaterThan";
      case TokenType::GREATER_EQUAL:
         return "GreaterEqual";
      case TokenType::AND:
         return "And";
      case TokenType::OR:
         return "Or";
      case TokenType::NOT:
         return "Not";
      case TokenType::MINUS:
         return "Minus";
      case TokenType::LEFT_PAREN:
         return "LeftParen";
      case TokenType::RIGHT_PAREN:
         return "RightParen";
      case TokenType::LEFT_BRACE:
         return "LeftBrace";
      case TokenType::RIGHT_BRACE:
         return "RightBrace";
      case TokenType::LEFT_BRACKET:
         return "LeftBracket";
      case TokenType::RIGHT_BRACKET:
         return "RightBracket";
      case TokenType::COMMA:
         return "Comma";
      case TokenType::END_OF_FILE:
         return "Eof";
   }
   return "Unknown";
}

std::string Token::toString() const {
   if (type == TokenType::STRING_LITERAL) {
      return fmt::format("Token({}, '{}')", tokenTypeToString(type), getStringValue());
   }
   if (type == TokenType::INT_LITERAL) {
      return fmt::format("Token({}, {})", tokenTypeToString(type), getIntValue());
   }
   if (type == TokenType::FLOAT_LITERAL) {
      return fmt::format("Token({}, {})", tokenTypeToString(type), getFloatValue());
   }
   if (type == TokenType::BOOL_LITERAL) {
      return fmt::format(
         "Token({}, {})", tokenTypeToString(type), getBoolValue() ? "true" : "false"
      );
   }
   if (type == TokenType::IDENTIFIER) {
      return fmt::format("Token({}, {})", tokenTypeToString(type), getStringValue());
   }
   return fmt::format("Token({})", tokenTypeToString(type));
}

std::string Token::getStringValue() const {
   return std::get<std::string>(value);
}

uint64_t Token::getIntValue() const {
   return std::get<uint64_t>(value);
}

double Token::getFloatValue() const {
   return std::get<double>(value);
}

bool Token::getBoolValue() const {
   return std::get<bool>(value);
}

}  // namespace rhydb::query_engine::saneql
