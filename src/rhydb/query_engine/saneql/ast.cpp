#include "rhydb/query_engine/saneql/ast.h"

#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/illegal_query_exception.h"

namespace rhydb::query_engine::saneql::ast {

std::string binaryOpToString(BinaryOp op) {
   switch (op) {
      case BinaryOp::AND:
         return "&&";
      case BinaryOp::OR:
         return "||";
      case BinaryOp::EQUALS:
         return "=";
      case BinaryOp::NOT_EQUALS:
         return "<>";
      case BinaryOp::LESS_THAN:
         return "<";
      case BinaryOp::LESS_EQUAL:
         return "<=";
      case BinaryOp::GREATER_THAN:
         return ">";
      case BinaryOp::GREATER_EQUAL:
         return ">=";
   }
   RHYDB_UNREACHABLE();
}

namespace {

std::string quote(std::string_view value, char quote_character) {
   std::string result{quote_character};
   for (const char character : value) {
      result += character;
      if (character == quote_character) {
         result += quote_character;
      }
   }
   result += quote_character;
   return result;
}

}  // namespace

std::string quoteStringLiteral(std::string_view value) {
   return quote(value, '\'');
}

std::string quoteIdentifier(std::string_view name) {
   return quote(name, '"');
}

namespace {

struct ExprToString {
   std::string operator()(const IntLiteral& lit) const { return std::to_string(lit.value); }

   std::string operator()(const FloatLiteral& lit) const { return fmt::format("{}", lit.value); }

   std::string operator()(const StringLiteral& lit) const { return quoteStringLiteral(lit.value); }

   std::string operator()(const BoolLiteral& lit) const { return lit.value ? "true" : "false"; }

   std::string operator()(const NullLiteral& /*unused*/) const { return "null"; }

   std::string operator()(const Identifier& identifier) const { return identifier.name; }

   std::string operator()(const BinaryExpr& expr) const {
      return fmt::format(
         "({} {} {})", expr.left->toString(), binaryOpToString(expr.op), expr.right->toString()
      );
   }

   std::string operator()(const UnaryNotExpr& expr) const {
      return fmt::format("(!{})", expr.operand->toString());
   }

   std::string operator()(const FunctionCall& call) const {
      std::string args;
      for (size_t i = 0; i < call.positional_arguments.size(); i++) {
         if (i > 0) {
            args += ", ";
         }
         args += call.positional_arguments[i].value->toString();
      }
      for (const auto& named_argument : call.named_arguments) {
         if (!args.empty()) {
            args += ", ";
         }
         args += named_argument.name + ":=" + named_argument.value->toString();
      }
      return fmt::format("{}({})", call.function_name, args);
   }

   std::string operator()(const TypeCast& cast) const {
      return fmt::format("{}::{}", cast.operand->toString(), cast.target_type);
   }

   std::string operator()(const SetLiteral& set) const {
      std::string elements;
      for (size_t i = 0; i < set.elements.size(); i++) {
         if (i > 0) {
            elements += ", ";
         }
         elements += set.elements[i]->toString();
      }
      return fmt::format("{{{}}}", elements);
   }

   std::string operator()(const RecordLiteral& record) const {
      std::string fields;
      for (size_t i = 0; i < record.fields.size(); i++) {
         if (i > 0) {
            fields += ", ";
         }
         fields += record.fields[i].name + ":=" + record.fields[i].value->toString();
      }
      return fmt::format("{{{}}}", fields);
   }
};

}  // namespace

std::string Expression::toString() const {
   return std::visit(ExprToString{}, value);
}

ExpressionPtr makeExpr(ExpressionVariant value, SourceLocation location) {
   auto expr = std::make_unique<Expression>();
   expr->value = std::move(value);
   expr->location = location;
   return expr;
}

std::string extractIdentifierName(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<Identifier>(expression.value),
      "expected identifier at {}:{}",
      expression.location.line,
      expression.location.column
   );
   return std::get<Identifier>(expression.value).name;
}

std::string extractStringLiteral(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<StringLiteral>(expression.value),
      "expected string literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   return std::get<StringLiteral>(expression.value).value;
}

uint32_t extractUint32Literal(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<IntLiteral>(expression.value),
      "expected integer literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   int64_t parsed_value = std::get<IntLiteral>(expression.value).value;
   CHECK_RHYDB_QUERY(
      std::cmp_greater_equal(parsed_value, std::numeric_limits<uint32_t>::min()),
      "Cannot cast {} to uint32. Value out of range",
      parsed_value
   );
   CHECK_RHYDB_QUERY(
      std::cmp_less_equal(parsed_value, std::numeric_limits<uint32_t>::max()),
      "Cannot cast {} to uint32. Value out of range",
      parsed_value
   );
   return static_cast<uint32_t>(parsed_value);
}

int32_t extractInt32Literal(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<IntLiteral>(expression.value),
      "expected integer literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   int64_t parsed_value = std::get<IntLiteral>(expression.value).value;
   CHECK_RHYDB_QUERY(
      parsed_value >= std::numeric_limits<int32_t>::min(),
      "Cannot cast {} to int32. Value out of range",
      parsed_value
   );
   CHECK_RHYDB_QUERY(
      parsed_value <= std::numeric_limits<int32_t>::max(),
      "Cannot cast {} to int32. Value out of range",
      parsed_value
   );
   return static_cast<int32_t>(parsed_value);
}

int64_t extractInt64Literal(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<IntLiteral>(expression.value),
      "expected integer literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   return std::get<IntLiteral>(expression.value).value;
}

double extractNumericAsFloatLiteral(const Expression& expression) {
   if (std::holds_alternative<FloatLiteral>(expression.value)) {
      return std::get<FloatLiteral>(expression.value).value;
   }
   if (std::holds_alternative<IntLiteral>(expression.value)) {
      return static_cast<double>(std::get<IntLiteral>(expression.value).value);
   }
   throw query_engine::IllegalQueryException(
      "expected numeric literal at {}:{}", expression.location.line, expression.location.column
   );
}

bool extractBoolLiteral(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<BoolLiteral>(expression.value),
      "expected boolean literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   return std::get<BoolLiteral>(expression.value).value;
}

common::Date32 extractDateValue(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<TypeCast>(expression.value),
      "expected date type cast at {}:{}",
      expression.location.line,
      expression.location.column
   );
   const auto& cast = std::get<TypeCast>(expression.value);
   CHECK_RHYDB_QUERY(
      cast.target_type == "date",
      "expected cast to 'date', got '{}' at {}:{}",
      cast.target_type,
      expression.location.line,
      expression.location.column
   );
   auto date_string = extractStringLiteral(*cast.operand);
   auto result = common::stringToDate32(date_string);
   CHECK_RHYDB_QUERY(
      result.has_value(),
      "invalid date '{}' at {}:{}: {}",
      date_string,
      expression.location.line,
      expression.location.column,
      result.error()
   );
   return result.value();
}

std::optional<common::Date32> extractOptionalDateValue(const Expression& expression) {
   if (std::holds_alternative<NullLiteral>(expression.value)) {
      return std::nullopt;
   }
   return extractDateValue(expression);
}

std::vector<std::string> extractSetOfIdentifiers(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<SetLiteral>(expression.value),
      "expected set literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   const auto& set = std::get<SetLiteral>(expression.value);
   std::vector<std::string> result;
   result.reserve(set.elements.size());
   for (const auto& elem : set.elements) {
      result.push_back(extractIdentifierName(*elem));
   }
   return result;
}

const SetLiteral& extractSetLiteral(const Expression& expression) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<SetLiteral>(expression.value),
      "expected set literal at {}:{}",
      expression.location.line,
      expression.location.column
   );
   return std::get<SetLiteral>(expression.value);
}

bool isDateExpression(const Expression& expression) {
   if (!std::holds_alternative<TypeCast>(expression.value)) {
      return false;
   }
   return std::get<TypeCast>(expression.value).target_type == "date";
}

bool isNullLiteral(const Expression& expression) {
   return std::holds_alternative<NullLiteral>(expression.value);
}

bool isIntLiteral(const Expression& expression) {
   return std::holds_alternative<IntLiteral>(expression.value);
}

bool isFloatLiteral(const Expression& expression) {
   return std::holds_alternative<FloatLiteral>(expression.value);
}

bool isStringLiteral(const Expression& expression) {
   return std::holds_alternative<StringLiteral>(expression.value);
}

bool isBoolLiteral(const Expression& expression) {
   return std::holds_alternative<BoolLiteral>(expression.value);
}

}  // namespace rhydb::query_engine::saneql::ast
