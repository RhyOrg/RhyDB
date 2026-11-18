#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "rhydb/query_engine/saneql/source_location.h"
#include "rhydb/query_engine/saneql/token.h"

namespace rhydb::query_engine::saneql {

class Lexer {
   std::string_view input;
   size_t position = 0;
   SourceLocation current_location;

  public:
   explicit Lexer(std::string_view input);

   [[nodiscard]] Token nextToken();
   [[nodiscard]] std::vector<Token> tokenizeAll();

  private:
   struct Codepoint {
      char32_t value;
      size_t byte_length;
   };

   [[nodiscard]] char peekByte() const;
   [[nodiscard]] char peekNextByte() const;
   /// Consumes one ASCII byte. Bytes >= 0x80 must go through peekCodepoint/consumeCodepoint.
   char advanceByte();
   /// Decodes the UTF-8 code point at the current position without consuming it.
   /// Throws ParseException if the input is not valid UTF-8 there.
   [[nodiscard]] Codepoint peekCodepoint() const;
   /// Consumes the UTF-8 code point at the current position and returns its raw bytes.
   /// Handles both ASCII and multi-byte code points and counts one column.
   /// Throws ParseException if the input is not valid UTF-8 there.
   std::string_view consumeCodepoint();
   [[nodiscard]] static bool isUnicodeWhitespace(char32_t codepoint);
   void skipWhitespace();
   [[nodiscard]] bool isAtEnd() const;

   [[nodiscard]] static Token makeToken(TokenType type, SourceLocation loc);
   [[nodiscard]] static Token makeToken(TokenType type, TokenValue value, SourceLocation loc);

   [[nodiscard]] Token readString();
   [[nodiscard]] Token readQuotedIdentifier();
   [[nodiscard]] Token readNumber();
   [[nodiscard]] Token readIdentifierOrKeyword();
};

}  // namespace rhydb::query_engine::saneql
