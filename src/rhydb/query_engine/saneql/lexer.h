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

   [[nodiscard]] char peek() const;
   [[nodiscard]] char peekNext() const;
   /// Consumes one ASCII byte. Bytes >= 0x80 must go through peekCodepoint/advanceCodepoint.
   char advance();
   /// Decodes the UTF-8 code point at the current position.
   /// Throws ParseException if the input is not valid UTF-8 there.
   [[nodiscard]] Codepoint peekCodepoint() const;
   /// Consumes a non-ASCII code point previously decoded with peekCodepoint. Counts one column.
   void advanceCodepoint(const Codepoint& codepoint);
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
