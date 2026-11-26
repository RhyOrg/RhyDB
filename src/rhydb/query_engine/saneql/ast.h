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
   EQUALS,
   NOT_EQUALS,
   LESS_THAN,
   LESS_EQUAL,
   GREATER_THAN,
   GREATER_EQUAL,
};

[[nodiscard]] std::string binaryOpToString(BinaryOp op);

enum class LogicalOp : uint8_t {
   AND,
   OR,
};

[[nodiscard]] std::string logicalOpToString(LogicalOp op);

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

/// An n-ary conjunction or disjunction, e.g. `a && b && c`. The parser collapses a chain of the
/// same operator into a single node, so that long chains (e.g. a filter on 100k accessions) do
/// not build a deeply nested AST, whose recursive passes would overflow the stack.
struct LogicalExpr {
   LogicalOp op;
   std::vector<ExpressionPtr> operands;
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
   LogicalExpr,
   UnaryNotExpr,
   FunctionCall,
   TypeCast,
   SetLiteral,
   RecordLiteral>;

struct Expression {
   ExpressionVariant value;
   SourceLocation location;
   /// The height of the expression tree, i.e. 1 for a leaf and otherwise 1 + the maximum depth of
   /// the children, computed by makeExpr
   uint32_t depth = 1;

   [[nodiscard]] std::string toString() const;
};

/// The type of a parameter of a definition, which determines what an argument may be and how it is
/// substituted. These are SaneQL's parameter types.
enum class ParameterType : uint8_t {
   /// `scalar`: a constant value, e.g. `true`, `5` or `'2021-01-01'::date`. The argument must not
   /// refer to any column or table.
   SCALAR,
   /// `expression`: any expression, substituted as written and resolved where it ends up in the
   /// body, e.g. a predicate over the columns of a table that the body builds.
   EXPRESSION,
   /// `table`: a table expression, e.g. a table name or a pipeline `data.filter(...)`.
   TABLE,
   /// `symbol`: a name, substituted wherever the parameter appears as a name in the body: as a
   /// column or table reference, and as the name of a record field (e.g. in `map({x := ...})`).
   SYMBOL,
};

/// The type named `name` in a parameter declaration (`scalar`, `table`, `expression` or `symbol`),
/// if any.
[[nodiscard]] std::optional<ParameterType> parameterTypeFromName(std::string_view name);

/// A parameter `name type [:= default]` of a FunctionDefinition. Without a default value, every
/// call must bind it.
struct DefinitionParameter {
   std::string name;
   ParameterType type;
   ExpressionPtr default_value;
   SourceLocation location;
};

/// `let name(parameter, ...) := body,`, or `let name := body,` without parameters.
struct FunctionDefinition {
   std::string name;
   std::vector<DefinitionParameter> parameters;
   ExpressionPtr body;
   SourceLocation location;
};

/// A query: the function definitions it starts with, followed by the expression to evaluate.
struct Program {
   std::vector<FunctionDefinition> definitions;
   ExpressionPtr body;
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
