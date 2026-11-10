#include "rhydb/query_engine/saneql/lexer.h"

#include <cctype>

#include <fast_float/fast_float.h>
#include <fmt/format.h>
#include <simdutf.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/saneql/parse_exception.h"

namespace rhydb::query_engine::saneql {

namespace {

constexpr unsigned char FIRST_NON_ASCII_BYTE = 0x80;
constexpr std::string_view UTF8_BYTE_ORDER_MARK = "\xEF\xBB\xBF";

bool isAsciiByte(char byte) {
   return static_cast<unsigned char>(byte) < FIRST_NON_ASCII_BYTE;
}

size_t utf8SequenceLength(unsigned char lead_byte) {
   if (lead_byte >= 0xC2 && lead_byte <= 0xDF) {
      return 2;
   }
   if (lead_byte >= 0xE0 && lead_byte <= 0xEF) {
      return 3;
   }
   if (lead_byte >= 0xF0 && lead_byte <= 0xF4) {
      return 4;
   }
   return 0;
}

}  // namespace

Lexer::Lexer(std::string_view input)
    : input(input) {
   // A leading byte order mark is not part of the query. It does not count as a column.
   if (input.starts_with(UTF8_BYTE_ORDER_MARK)) {
      position = UTF8_BYTE_ORDER_MARK.size();
   }
}

bool Lexer::isAtEnd() const {
   return position >= input.size();
}

char Lexer::peek() const {
   if (isAtEnd()) {
      return '\0';
   }
   return input[position];
}

char Lexer::peekNext() const {
   if (position + 1 >= input.size()) {
      return '\0';
   }
   return input[position + 1];
}

char Lexer::advance() {
   RHYDB_ASSERT(!isAtEnd());
   const char current = input[position];
   RHYDB_ASSERT(isAsciiByte(current));
   position++;
   if (current == '\n') {
      current_location.line++;
      current_location.column = 1;
   } else {
      current_location.column++;
   }
   return current;
}

Lexer::Codepoint Lexer::peekCodepoint() const {
   RHYDB_ASSERT(!isAtEnd());
   const auto lead_byte = static_cast<unsigned char>(input[position]);
   if (lead_byte < FIRST_NON_ASCII_BYTE) {
      return {.value = lead_byte, .byte_length = 1};
   }

   const size_t byte_length = utf8SequenceLength(lead_byte);
   if (byte_length == 0 || position + byte_length > input.size()) {
      throw ParseException(current_location, "Invalid UTF-8 byte 0x{:02X}", lead_byte);
   }

   char32_t value = 0;
   const auto result =
      simdutf::convert_utf8_to_utf32_with_errors(input.data() + position, byte_length, &value);
   if (result.error != simdutf::error_code::SUCCESS || result.count != 1) {
      throw ParseException(current_location, "Invalid UTF-8 byte 0x{:02X}", lead_byte);
   }
   return {.value = value, .byte_length = byte_length};
}

void Lexer::advanceCodepoint(const Codepoint& codepoint) {
   RHYDB_ASSERT(codepoint.value >= FIRST_NON_ASCII_BYTE);
   RHYDB_ASSERT(position + codepoint.byte_length <= input.size());
   position += codepoint.byte_length;
   current_location.column++;
}

bool Lexer::isUnicodeWhitespace(char32_t codepoint) {
   // Unicode White_Space property
   return (codepoint >= 0x0009 && codepoint <= 0x000D) || codepoint == 0x0020 ||
          codepoint == 0x0085 || codepoint == 0x00A0 || codepoint == 0x1680 ||
          (codepoint >= 0x2000 && codepoint <= 0x200A) || codepoint == 0x2028 ||
          codepoint == 0x2029 || codepoint == 0x202F || codepoint == 0x205F || codepoint == 0x3000;
}

void Lexer::skipWhitespace() {
   while (!isAtEnd()) {
      const char current = peek();
      if (
         current == ' ' || current == '\t' || current == '\n' || current == '\v' ||
         current == '\f' || current == '\r'
      ) {
         advance();
      } else if (current == '-' && peekNext() == '-') {
         while (!isAtEnd() && peek() != '\n') {
            if (isAsciiByte(peek())) {
               advance();
            } else {
               advanceCodepoint(peekCodepoint());
            }
         }
      } else if (!isAsciiByte(current)) {
         const Codepoint codepoint = peekCodepoint();
         if (!isUnicodeWhitespace(codepoint.value)) {
            break;
         }
         advanceCodepoint(codepoint);
      } else {
         break;
      }
   }
}

Token Lexer::makeToken(TokenType type, SourceLocation loc) {
   return makeToken(type, std::monostate{}, loc);
}

Token Lexer::makeToken(TokenType type, TokenValue value, SourceLocation loc) {
   return Token{.type = type, .value = std::move(value), .location = loc};
}

Token Lexer::readString() {
   const SourceLocation start = current_location;
   RHYDB_ASSERT(peek() == '\'');
   advance();

   std::string result;
   while (!isAtEnd()) {
      if (peek() == '\'') {
         advance();
         if (!isAtEnd() && peek() == '\'') {
            advance();
            // Escaped single quote: '' -> '
            result += '\'';
         } else {
            return makeToken(TokenType::STRING_LITERAL, std::move(result), start);
         }
      } else if (isAsciiByte(peek())) {
         result += advance();
      } else {
         const Codepoint codepoint = peekCodepoint();
         result += input.substr(position, codepoint.byte_length);
         advanceCodepoint(codepoint);
      }
   }
   throw ParseException(start, "Unterminated string literal");
}

Token Lexer::readQuotedIdentifier() {
   const SourceLocation start = current_location;

   RHYDB_ASSERT(peek() == '"');
   advance();

   std::string result;
   while (!isAtEnd()) {
      if (peek() == '"') {
         advance();
         if (!isAtEnd() && peek() == '"') {
            advance();
            // Escaped double quote: "" -> "
            result += '"';
         } else {
            return makeToken(TokenType::IDENTIFIER, std::move(result), start);
         }
      } else if (isAsciiByte(peek())) {
         result += advance();
      } else {
         const Codepoint codepoint = peekCodepoint();
         result += input.substr(position, codepoint.byte_length);
         advanceCodepoint(codepoint);
      }
   }
   throw ParseException(start, "Unterminated quoted identifier");
}

Token Lexer::readNumber() {
   const SourceLocation start = current_location;
   const size_t num_start = position;

   bool is_float = false;
   while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '.')) {
      const char current = advance();
      if (current == '.') {
         is_float = true;
      } else if (current == 'e' || current == 'E') {
         // Scientific notation such as 9.0E-4
         is_float = true;
         if (!isAtEnd() && (peek() == '+' || peek() == '-')) {
            advance();
         }
      }
   }

   const std::string_view num_str = input.substr(num_start, position - num_start);

   if (is_float) {
      double val = 0;
      auto [ptr, ec] = fast_float::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
      if (ec != std::errc() || ptr != num_str.data() + num_str.size()) {
         throw ParseException(start, "Invalid float literal");
      }
      return makeToken(TokenType::FLOAT_LITERAL, val, start);
   }

   uint64_t val = 0;
   auto [ptr, ec] = fast_float::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
   if (ec != std::errc() || ptr != num_str.data() + num_str.size()) {
      throw ParseException(start, "Invalid integer literal");
   }
   return makeToken(TokenType::INT_LITERAL, val, start);
}

Token Lexer::readIdentifierOrKeyword() {
   const SourceLocation start = current_location;
   const size_t id_start = position;

   while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
      advance();
   }

   std::string identifier(input.substr(id_start, position - id_start));

   if (identifier == "true") {
      return makeToken(TokenType::BOOL_LITERAL, true, start);
   }
   if (identifier == "false") {
      return makeToken(TokenType::BOOL_LITERAL, false, start);
   }
   if (identifier == "null") {
      return makeToken(TokenType::NULL_LITERAL, std::monostate{}, start);
   }

   return makeToken(TokenType::IDENTIFIER, std::move(identifier), start);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
Token Lexer::nextToken() {
   skipWhitespace();

   if (isAtEnd()) {
      return makeToken(TokenType::END_OF_FILE, current_location);
   }

   const SourceLocation start = current_location;
   char current = peek();

   if (current == '"') {
      return readQuotedIdentifier();
   }

   if (current == '\'') {
      return readString();
   }

   if (std::isdigit(static_cast<unsigned char>(current))) {
      return readNumber();
   }

   if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
      return readIdentifierOrKeyword();
   }

   switch (current) {
      case '.':
         advance();
         return makeToken(TokenType::DOT, start);
      case ',':
         advance();
         return makeToken(TokenType::COMMA, start);
      case '(':
         advance();
         return makeToken(TokenType::LEFT_PAREN, start);
      case ')':
         advance();
         return makeToken(TokenType::RIGHT_PAREN, start);
      case '{':
         advance();
         return makeToken(TokenType::LEFT_BRACE, start);
      case '}':
         advance();
         return makeToken(TokenType::RIGHT_BRACE, start);
      case '[':
         advance();
         return makeToken(TokenType::LEFT_BRACKET, start);
      case ']':
         advance();
         return makeToken(TokenType::RIGHT_BRACKET, start);
      case '!':
         advance();
         return makeToken(TokenType::NOT, start);
      case '=':
         advance();
         return makeToken(TokenType::EQUALS, start);
      case '<':
         advance();
         if (!isAtEnd() && peek() == '>') {
            advance();
            return makeToken(TokenType::NOT_EQUALS, start);
         }
         if (!isAtEnd() && peek() == '=') {
            advance();
            return makeToken(TokenType::LESS_EQUAL, start);
         }
         return makeToken(TokenType::LESS_THAN, start);
      case '>':
         advance();
         if (!isAtEnd() && peek() == '=') {
            advance();
            return makeToken(TokenType::GREATER_EQUAL, start);
         }
         return makeToken(TokenType::GREATER_THAN, start);
      case '&':
         advance();
         if (!isAtEnd() && peek() == '&') {
            advance();
            return makeToken(TokenType::AND, start);
         }
         throw ParseException(start, "Expected '&&'");
      case '|':
         advance();
         if (!isAtEnd() && peek() == '|') {
            advance();
            return makeToken(TokenType::OR, start);
         }
         throw ParseException(start, "Expected '||'");
      case ':':
         advance();
         if (!isAtEnd() && peek() == ':') {
            advance();
            return makeToken(TokenType::DOUBLE_COLON, start);
         }
         if (!isAtEnd() && peek() == '=') {
            advance();
            return makeToken(TokenType::COLON_EQUALS, start);
         }
         throw ParseException(start, "Expected '::' or ':='");
      case '-':
         advance();
         return makeToken(TokenType::MINUS, start);
      default:
         if (!isAsciiByte(current)) {
            // Throws for invalid UTF-8; otherwise the character is valid but not allowed here.
            const Codepoint codepoint = peekCodepoint();
            throw ParseException(
               start, "Unexpected character U+{:04X}", static_cast<uint32_t>(codepoint.value)
            );
         }
         advance();
         throw ParseException(start, "Unexpected character '{}'", current);
   }
}

std::vector<Token> Lexer::tokenizeAll() {
   std::vector<Token> tokens;
   while (true) {
      const Token token = nextToken();
      tokens.push_back(token);
      if (token.type == TokenType::END_OF_FILE) {
         break;
      }
   }
   return tokens;
}

}  // namespace rhydb::query_engine::saneql
