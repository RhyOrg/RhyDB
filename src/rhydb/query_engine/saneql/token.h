#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "rhydb/query_engine/saneql/source_location.h"

namespace rhydb::query_engine::saneql {

enum class TokenType : uint8_t {
   INT_LITERAL,
   FLOAT_LITERAL,
   STRING_LITERAL,
   BOOL_LITERAL,
   NULL_LITERAL,
   IDENTIFIER,
   DOT,
   DOUBLE_COLON,
   COLON_EQUALS,
   EQUALS,
   NOT_EQUALS,
   LESS_THAN,
   LESS_EQUAL,
   GREATER_THAN,
   GREATER_EQUAL,
   AND,
   OR,
   NOT,
   MINUS,
   LEFT_PAREN,
   RIGHT_PAREN,
   LEFT_BRACE,
   RIGHT_BRACE,
   LEFT_BRACKET,
   RIGHT_BRACKET,
   COMMA,
   END_OF_FILE,
};

[[nodiscard]] std::string tokenTypeToString(TokenType type);

using TokenValue = std::variant<std::monostate, uint64_t, double, std::string, bool>;

struct Token {
   TokenType type;
   TokenValue value;
   SourceLocation location;

   [[nodiscard]] std::string toString() const;

   [[nodiscard]] std::string getStringValue() const;
   [[nodiscard]] uint64_t getIntValue() const;
   [[nodiscard]] double getFloatValue() const;
   [[nodiscard]] bool getBoolValue() const;
};

}  // namespace rhydb::query_engine::saneql
