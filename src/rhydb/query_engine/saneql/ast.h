#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "rhydb/common/date32.h"
#include "rhydb/query_engine/saneql/source_location.h"

namespace rhydb::query_engine::saneql::ast {

struct Expression;

using ExpressionPtr = std::unique_ptr<Expression>;

struct PositionalArgument {
   ExpressionPtr value;
   SourceLocation location;
};

struct NamedArgument {
   std::string name;
   ExpressionPtr value;
   SourceLocation location;
};

enum class BinaryOp : uint8_t {
   AND,
   OR,
   EQUALS,
   NOT_EQUALS,
   LESS_THAN,
   LESS_EQUAL,
   GREATER_THAN,
   GREATER_EQUAL,
};

[[nodiscard]] std::string binaryOpToString(BinaryOp op);

/// `value` as a string literal that parses back to `value`, i.e. quoted with every `'` doubled.
[[nodiscard]] std::string quoteStringLiteral(std::string_view value);

/// `name` as a quoted identifier that parses back to `name`, i.e. quoted with every `"` doubled.
[[nodiscard]] std::string quoteIdentifier(std::string_view name);

struct IntLiteral {
   int64_t value;
};

struct FloatLiteral {
   double value;
};

struct StringLiteral {
   std::string value;
};

struct BoolLiteral {
   bool value;
};

struct NullLiteral {};

struct Identifier {
   std::string name;
};

struct BinaryExpr {
   BinaryOp op;
   ExpressionPtr left;
   ExpressionPtr right;
};

struct UnaryNotExpr {
   ExpressionPtr operand;
};

struct FunctionCall {
   std::string function_name;
   std::vector<PositionalArgument> positional_arguments;
   std::vector<NamedArgument> named_arguments;
};

struct TypeCast {
   ExpressionPtr operand;
   std::string target_type;
};

struct SetLiteral {
   std::vector<ExpressionPtr> elements;
};

struct RecordField {
   std::string name;
   ExpressionPtr value;
};

struct RecordLiteral {
   std::vector<RecordField> fields;
};

using ExpressionVariant = std::variant<
   IntLiteral,
   FloatLiteral,
   StringLiteral,
   BoolLiteral,
   NullLiteral,
   Identifier,
   BinaryExpr,
   UnaryNotExpr,
   FunctionCall,
   TypeCast,
   SetLiteral,
   RecordLiteral>;

struct Expression {
   ExpressionVariant value;
   SourceLocation location;

   [[nodiscard]] std::string toString() const;
};

ExpressionPtr makeExpr(ExpressionVariant value, SourceLocation location);

[[nodiscard]] std::string extractIdentifierName(const Expression& expression);
[[nodiscard]] std::string extractStringLiteral(const Expression& expression);
[[nodiscard]] uint32_t extractUint32Literal(const Expression& expression);
[[nodiscard]] int32_t extractInt32Literal(const Expression& expression);
[[nodiscard]] int64_t extractInt64Literal(const Expression& expression);
[[nodiscard]] double extractNumericAsFloatLiteral(const Expression& expression);
[[nodiscard]] bool extractBoolLiteral(const Expression& expression);
[[nodiscard]] common::Date32 extractDateValue(const Expression& expression);
[[nodiscard]] std::optional<common::Date32> extractOptionalDateValue(const Expression& expression);
[[nodiscard]] std::vector<std::string> extractSetOfIdentifiers(const Expression& expression);
[[nodiscard]] const SetLiteral& extractSetLiteral(const Expression& expression);

[[nodiscard]] bool isDateExpression(const Expression& expression);
[[nodiscard]] bool isNullLiteral(const Expression& expression);
[[nodiscard]] bool isIntLiteral(const Expression& expression);
[[nodiscard]] bool isFloatLiteral(const Expression& expression);
[[nodiscard]] bool isStringLiteral(const Expression& expression);
[[nodiscard]] bool isBoolLiteral(const Expression& expression);

}  // namespace rhydb::query_engine::saneql::ast
