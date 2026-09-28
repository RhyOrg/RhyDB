#include "rhydb/query_engine/saneql/ast_to_query.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <re2/re2.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/join_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/schema_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/tables_node.h"
#include "rhydb/query_engine/operators/transitive_closure_node.h"
#include "rhydb/query_engine/operators/union_all_node.h"
#include "rhydb/query_engine/operators/unresolved_insertions_node.h"
#include "rhydb/query_engine/operators/unresolved_most_recent_common_ancestor_node.h"
#include "rhydb/query_engine/operators/unresolved_mutations_node.h"
#include "rhydb/query_engine/operators/unresolved_phylo_subtree_node.h"
#include "rhydb/query_engine/order_by_field.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/query_engine/saneql/parser.h"
#include "rhydb/query_engine/scalar_expressions/and.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/comparison.h"
#include "rhydb/query_engine/scalar_expressions/date_between.h"
#include "rhydb/query_engine/scalar_expressions/exact.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/float_between.h"
#include "rhydb/query_engine/scalar_expressions/has_mutation.h"
#include "rhydb/query_engine/scalar_expressions/insertion_contains.h"
#include "rhydb/query_engine/scalar_expressions/int_between.h"
#include "rhydb/query_engine/scalar_expressions/is_null.h"
#include "rhydb/query_engine/scalar_expressions/iso_week.h"
#include "rhydb/query_engine/scalar_expressions/lineage_filter.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/maybe.h"
#include "rhydb/query_engine/scalar_expressions/mutation_profile.h"
#include "rhydb/query_engine/scalar_expressions/negation.h"
#include "rhydb/query_engine/scalar_expressions/nof.h"
#include "rhydb/query_engine/scalar_expressions/or.h"
#include "rhydb/query_engine/scalar_expressions/phylo_child_filter.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/string_search.h"
#include "rhydb/query_engine/scalar_expressions/symbol_equals.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"
#include "rhydb/storage/column/column_type_visitor.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

using rhydb::AminoAcid;
using rhydb::Nucleotide;
using rhydb::query_engine::filter::operators::Comparator;
using rhydb::query_engine::saneql::ast::BinaryOp;
using rhydb::storage::column::Column;
using rhydb::storage::column::SequenceColumn;
using rhydb::storage::column::ZstdCompressedStringColumn;

namespace rhydb::query_engine::saneql {

namespace {

/// Resolves a column name against the input's output schema so that filter leaf
/// expressions can carry the column's actual {name, type}.
schema::ColumnIdentifier resolveColumn(
   const std::string& column_name,
   const std::vector<schema::ColumnIdentifier>& schema
) {
   const auto found =
      std::ranges::find_if(schema, [&](const auto& col) { return col.name == column_name; });
   CHECK_RHYDB_QUERY(
      found != schema.end(), "The database does not contain the column '{}'", column_name
   );
   return *found;
}

/// Builds a scalar Expression from a function call (e.g. `seq.at(3)`) appearing in
/// a scalar position. The handler resolves referenced columns against `schema`;
/// this additionally validates that every referenced column exists so a missing
/// column produces a diagnostic with the call's context and location.
std::unique_ptr<scalar_expressions::ScalarExpression> convertScalarFunctionCall(
   const ast::FunctionCall& call,
   const SourceLocation& location,
   const std::vector<schema::ColumnIdentifier>& schema,
   std::string_view error_context,
   const Tables& tables
) {
   const auto* entry = ScalarFunctionRegistry::instance().findFunction(call.function_name);
   CHECK_RHYDB_QUERY(
      entry != nullptr,
      "{} references unknown scalar function '{}' at {}:{}",
      error_context,
      call.function_name,
      location.line,
      location.column
   );
   auto bound = bindArguments(
      call.function_name, entry->signature, call.positional_arguments, call.named_arguments
   );
   auto expression = entry->handler(bound, schema, tables);
   // Validate that every referenced column actually exists, mirroring the check for
   // a bare column reference.
   for (const auto& referenced : expression->freeIUs()) {
      const bool exists =
         std::ranges::any_of(schema, [&](const auto& col) { return col.name == referenced.name; });
      CHECK_RHYDB_QUERY(
         exists,
         "{} references unknown column '{}' at {}:{}",
         error_context,
         referenced.name,
         location.line,
         location.column
      );
   }
   return expression;
}

/// Converts a saneql expression into a RhyDB scalar Expression: a value-producing
/// expression (as opposed to convertToFilter, which yields a boolean predicate).
/// Supported forms are literals (int, float, string, bool), references to an
/// existing column of `schema` (resolved to that column's type), and scalar
/// function calls (see convertScalarFunctionCall). `error_context` prefixes
/// diagnostics so callers can describe where the expression appears
/// (e.g. "map() field 'x'").
std::unique_ptr<scalar_expressions::ScalarExpression> convertToScalar(
   const ast::Expression& ast,
   const std::vector<schema::ColumnIdentifier>& schema,
   std::string_view error_context,
   const Tables& tables
) {
   const auto& [value, location] = ast;

   if (std::holds_alternative<ast::Identifier>(value)) {
      const auto& name = std::get<ast::Identifier>(value).name;
      auto found = std::ranges::find_if(schema, [&](const auto& col) { return col.name == name; });
      CHECK_RHYDB_QUERY(
         found != schema.end(),
         "{} references unknown column '{}' at {}:{}",
         error_context,
         name,
         location.line,
         location.column
      );
      return std::make_unique<scalar_expressions::FieldRef>(*found);
   }
   if (std::holds_alternative<ast::IntLiteral>(value)) {
      // Integer literals are width-agnostic: the value is kept as a full-range int64
      // and the int32 range check (and column routing) happens at compile time, once
      // the actual column type is known.
      return std::make_unique<scalar_expressions::Int64Literal>(
         std::get<ast::IntLiteral>(value).value
      );
   }
   if (std::holds_alternative<ast::FloatLiteral>(value)) {
      return std::make_unique<scalar_expressions::FloatLiteral>(
         std::get<ast::FloatLiteral>(value).value
      );
   }
   if (std::holds_alternative<ast::StringLiteral>(value)) {
      return std::make_unique<scalar_expressions::StringLiteral>(
         std::get<ast::StringLiteral>(value).value
      );
   }
   if (std::holds_alternative<ast::BoolLiteral>(value)) {
      return std::make_unique<scalar_expressions::BoolLiteral>(
         std::get<ast::BoolLiteral>(value).value
      );
   }
   if (std::holds_alternative<ast::FunctionCall>(value)) {
      return convertScalarFunctionCall(
         std::get<ast::FunctionCall>(value), location, schema, error_context, tables
      );
   }
   if (isDateExpression(ast)) {
      return std::make_unique<scalar_expressions::DateLiteral>(extractDateValue(ast));
   }

   throw IllegalQueryException(
      "{} must be a literal value (int, float, string, bool, or date), a column reference, or a "
      "scalar function call at {}:{}",
      error_context,
      location.line,
      location.column
   );
}

/// Maps a comparison ast::BinaryOp to the filter operator comparator.
Comparator toComparator(BinaryOp binary_op) {
   switch (binary_op) {
      case BinaryOp::EQUALS:
         return Comparator::EQUALS;
      case BinaryOp::NOT_EQUALS:
         return Comparator::NOT_EQUALS;
      case BinaryOp::LESS_THAN:
         return Comparator::LESS;
      case BinaryOp::LESS_EQUAL:
         return Comparator::LESS_OR_EQUALS;
      case BinaryOp::GREATER_THAN:
         return Comparator::HIGHER;
      case BinaryOp::GREATER_EQUAL:
         return Comparator::HIGHER_OR_EQUALS;
      default:
         throw IllegalQueryException("unexpected operator for comparison");
   }
}

ScalarExpressionPtr convertBinaryExprToFilter(
   const ast::BinaryExpr& bin_expr,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   switch (bin_expr.op) {
      case BinaryOp::AND: {
         scalar_expressions::ScalarExpressionVector children;
         children.push_back(convertToFilter(*bin_expr.left, schema, tables));
         children.push_back(convertToFilter(*bin_expr.right, schema, tables));
         return std::make_unique<scalar_expressions::And>(std::move(children));
      }
      case BinaryOp::OR: {
         scalar_expressions::ScalarExpressionVector children;
         children.push_back(convertToFilter(*bin_expr.left, schema, tables));
         children.push_back(convertToFilter(*bin_expr.right, schema, tables));
         return std::make_unique<scalar_expressions::Or>(std::move(children));
      }
      case BinaryOp::EQUALS:
      case BinaryOp::NOT_EQUALS:
      case BinaryOp::LESS_THAN:
      case BinaryOp::LESS_EQUAL:
      case BinaryOp::GREATER_THAN:
      case BinaryOp::GREATER_EQUAL: {
         const Comparator comparator = toComparator(bin_expr.op);
         auto left =
            convertToScalar(*bin_expr.left, schema, "the left side of a comparison", tables);
         auto right =
            convertToScalar(*bin_expr.right, schema, "the right side of a comparison", tables);
         return std::make_unique<scalar_expressions::Comparison>(
            std::move(left), std::move(right), comparator
         );
      }
   }
   throw IllegalQueryException("unhandled binary operator");
}

// ========================================================================
// Scalar function handlers (registered in ScalarFunctionRegistry)
// ========================================================================

ScalarExpressionPtr handleBetween(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   auto column_name = extractIdentifierName(args.at("column"));
   const auto& from_expr = args.at("from");
   const auto& to_expr = args.at("to");

   if (isDateExpression(from_expr) || isDateExpression(to_expr)) {
      return std::make_unique<scalar_expressions::DateBetween>(
         resolveColumn(column_name, schema),
         extractOptionalDateValue(from_expr),
         extractOptionalDateValue(to_expr)
      );
   }
   if (isFloatLiteral(from_expr) || isFloatLiteral(to_expr)) {
      std::optional<double> from_val;
      std::optional<double> to_val;
      if (!isNullLiteral(from_expr)) {
         from_val = extractNumericAsFloatLiteral(from_expr);
      }
      if (!isNullLiteral(to_expr)) {
         to_val = extractNumericAsFloatLiteral(to_expr);
      }
      return std::make_unique<scalar_expressions::FloatBetween>(
         resolveColumn(column_name, schema), from_val, to_val
      );
   }
   if (isIntLiteral(from_expr) || isIntLiteral(to_expr)) {
      std::optional<int64_t> from_val;
      std::optional<int64_t> to_val;
      if (!isNullLiteral(from_expr)) {
         from_val = extractInt64Literal(from_expr);
      }
      if (!isNullLiteral(to_expr)) {
         to_val = extractInt64Literal(to_expr);
      }
      return std::make_unique<scalar_expressions::IntBetween>(
         resolveColumn(column_name, schema), from_val, to_val
      );
   }
   throw IllegalQueryException(
      "Could not infer type of between expression. From-value or to-value needs to be a typed "
      "non-null value, got from: '{}' to: '{}'",
      from_expr.toString(),
      to_expr.toString()
   );
}

/// `column = v1 || column = v2 || ...`, or `false` for no values. A bitmap union on indexed
/// columns.
ScalarExpressionPtr buildValueSetPredicate(
   const schema::ColumnIdentifier& column,
   std::vector<ScalarExpressionPtr> value_literals
) {
   if (value_literals.empty()) {
      return std::make_unique<scalar_expressions::BoolLiteral>(false);
   }
   scalar_expressions::ScalarExpressionVector comparisons;
   comparisons.reserve(value_literals.size());
   for (auto& literal : value_literals) {
      comparisons.push_back(std::make_unique<scalar_expressions::Comparison>(
         std::make_unique<scalar_expressions::FieldRef>(column),
         std::move(literal),
         Comparator::EQUALS
      ));
   }
   if (comparisons.size() == 1) {
      return std::move(comparisons.front());
   }
   return std::make_unique<scalar_expressions::Or>(std::move(comparisons));
}

ScalarExpressionPtr handleIn(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   const schema::ColumnIdentifier column =
      resolveColumn(extractIdentifierName(args.at("column")), schema);
   const auto& values_expr = args.at("values");

   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::SetLiteral>(values_expr.value),
      "in() expects a set literal argument at {}:{}",
      values_expr.location.line,
      values_expr.location.column
   );
   // Values may be of any column type.
   std::vector<ScalarExpressionPtr> value_literals;
   for (const auto& elem : std::get<ast::SetLiteral>(values_expr.value).elements) {
      value_literals.push_back(convertToScalar(*elem, schema, "in() value", tables));
   }
   return buildValueSetPredicate(column, std::move(value_literals));
}

ScalarExpressionPtr handleIsNull(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   return std::make_unique<scalar_expressions::IsNull>(
      resolveColumn(extractIdentifierName(args.at("column")), schema)
   );
}

ScalarExpressionPtr handleIsNotNull(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   return std::make_unique<scalar_expressions::Negation>(
      std::make_unique<scalar_expressions::IsNull>(
         resolveColumn(extractIdentifierName(args.at("column")), schema)
      )
   );
}

ScalarExpressionPtr handleLineage(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   auto column_name = extractIdentifierName(args.at("column"));
   const auto& value_expr = args.at("value");
   std::optional<std::string> lineage_value;
   if (!isNullLiteral(value_expr)) {
      lineage_value = extractStringLiteral(value_expr);
   }
   bool include_sublineages = false;
   if (const auto* expr = args.get("includeSublineages")) {
      include_sublineages = extractBoolLiteral(*expr);
   }
   std::optional<common::RecombinantEdgeFollowingMode> sublineage_mode;
   if (include_sublineages) {
      sublineage_mode = common::RecombinantEdgeFollowingMode::DO_NOT_FOLLOW;
   }
   auto recombinant_mode = args.getOptionalString("recombinantFollowingMode");
   if (recombinant_mode.has_value()) {
      if (recombinant_mode.value() == "alwaysFollow") {
         sublineage_mode = common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW;
      } else if (recombinant_mode.value() == "followIfFullyContainedInClade") {
         sublineage_mode = common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE;
      } else if (recombinant_mode.value() == "doNotFollow") {
         sublineage_mode = common::RecombinantEdgeFollowingMode::DO_NOT_FOLLOW;
      } else {
         throw IllegalQueryException(
            "invalid recombinantFollowingMode: '{}'. Valid values are: alwaysFollow, "
            "followIfFullyContainedInClade, doNotFollow",
            recombinant_mode.value()
         );
      }
   }
   return std::make_unique<scalar_expressions::LineageFilter>(
      resolveColumn(column_name, schema), lineage_value, sublineage_mode
   );
}

ScalarExpressionPtr handlePhyloDescendantOf(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   return std::make_unique<scalar_expressions::PhyloChildFilter>(
      resolveColumn(extractIdentifierName(args.at("column")), schema),
      extractStringLiteral(args.at("node"))
   );
}

ScalarExpressionPtr handleLike(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   auto column_name = extractIdentifierName(args.at("column"));
   auto pattern = extractStringLiteral(args.at("pattern"));
   auto regex = std::make_unique<re2::RE2>(pattern);
   CHECK_RHYDB_QUERY(
      regex->ok(),
      "Invalid Regular Expression. The parsing of the regular expression failed with the "
      "error '{}'. See https://github.com/google/re2/wiki/Syntax for a Syntax specification.",
      regex->error()
   );
   return std::make_unique<scalar_expressions::StringSearch>(
      resolveColumn(column_name, schema), std::move(regex)
   );
}

template <typename SymbolType>
ScalarExpressionPtr handleSymbolEquals(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   const uint32_t position = extractUint32Literal(args.at("position"));
   CHECK_RHYDB_QUERY(position > 0, "The field 'position' is 1-indexed. Value of 0 not allowed.");
   const uint32_t position_idx = position - 1;
   auto symbol_str = extractStringLiteral(args.at("symbol"));
   CHECK_RHYDB_QUERY(
      symbol_str.size() == 1, "{}() symbol must be a single character", args.functionName()
   );
   auto sequence_name = extractStringLiteral(args.at("sequenceName"));
   auto column = resolveColumn(sequence_name, schema);
   char symbol_char = symbol_str[0];
   if (symbol_char == '.') {
      return std::make_unique<scalar_expressions::SymbolEquals<SymbolType>>(
         column, position_idx, scalar_expressions::SymbolOrDot<SymbolType>::dot()
      );
   }
   auto symbol = SymbolType::charToSymbol(symbol_char);
   CHECK_RHYDB_QUERY(
      symbol.has_value(), "{}() invalid symbol '{}'", args.functionName(), symbol_char
   );
   return std::make_unique<scalar_expressions::SymbolEquals<SymbolType>>(
      column, position_idx, scalar_expressions::SymbolOrDot<SymbolType>(symbol.value())
   );
}

template <typename SymbolType>
ScalarExpressionPtr handleHasMutation(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   const uint32_t position = extractUint32Literal(args.at("position"));
   CHECK_RHYDB_QUERY(position > 0, "The field 'position' is 1-indexed. Value of 0 not allowed.");
   auto sequence_name = extractStringLiteral(args.at("sequenceName"));
   auto column = resolveColumn(sequence_name, schema);
   return std::make_unique<scalar_expressions::HasMutation<SymbolType>>(
      std::move(column), position - 1
   );
}

template <typename SymbolType>
ScalarExpressionPtr handleInsertionContains(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   auto position = extractUint32Literal(args.at("position"));
   auto value = extractStringLiteral(args.at("value"));
   CHECK_RHYDB_QUERY(
      !value.empty(),
      "The field 'value' in an InsertionContains expression must not be an empty string"
   );
   auto sequence_name = extractStringLiteral(args.at("sequenceName"));
   auto column = resolveColumn(sequence_name, schema);
   return std::make_unique<scalar_expressions::InsertionContains<SymbolType>>(
      std::move(column), position, std::move(value)
   );
}

ScalarExpressionPtr handleAt(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   auto input_column = extractIdentifierName(args.at("input"));
   const uint32_t position = extractUint32Literal(args.at("position"));
   CHECK_RHYDB_QUERY(
      position > 0, "at(): the field 'position' is 1-indexed. Value of 0 not allowed."
   );
   // Resolve the referenced column against the available schema so the At carries the
   // column's actual {name, type}. If it is unknown the column is kept with a
   // placeholder type; the caller reports the (contextual) unknown-column error.
   const auto found =
      std::ranges::find_if(schema, [&](const auto& col) { return col.name == input_column; });
   CHECK_RHYDB_QUERY(
      found != schema.end(), "at(): the field {} is not found in the current context", input_column
   );
   return std::make_unique<scalar_expressions::At>(
      std::make_unique<scalar_expressions::FieldRef>(*found), position
   );
}

ScalarExpressionPtr handleIsoWeek(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   auto input_column = extractIdentifierName(args.at("input"));
   const auto found =
      std::ranges::find_if(schema, [&](const auto& col) { return col.name == input_column; });
   CHECK_RHYDB_QUERY(
      found != schema.end(),
      "isoWeek(): the field {} is not found in the current context",
      input_column
   );
   CHECK_RHYDB_QUERY(
      found->type == schema::ColumnType::DATE32,
      "isoWeek(): the field {} must be a date column",
      input_column
   );
   return std::make_unique<scalar_expressions::IsoWeek>(
      std::make_unique<scalar_expressions::FieldRef>(*found)
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
ScalarExpressionPtr handleExact(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   return std::make_unique<scalar_expressions::Exact>(
      convertToFilter(args.at("child"), schema, tables)
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
ScalarExpressionPtr handleMaybe(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   return std::make_unique<scalar_expressions::Maybe>(
      convertToFilter(args.at("child"), schema, tables)
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
ScalarExpressionPtr handleNOf(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   const int32_t number_of_matchers = extractInt32Literal(args.at("count"));
   bool match_exactly = false;
   if (const auto* expr = args.get("matchExactly")) {
      match_exactly = extractBoolLiteral(*expr);
   }
   const auto& children_set = extractSetLiteral(args.at("children"));
   scalar_expressions::ScalarExpressionVector children;
   for (const auto& child_expr : children_set.elements) {
      children.push_back(convertToFilter(*child_expr, schema, tables));
   }
   return std::make_unique<scalar_expressions::NOf>(
      std::move(children), number_of_matchers, match_exactly
   );
}

template <typename SymbolType>
scalar_expressions::MutationProfile<SymbolType>::Mutation parseMutationRecord(
   const ast::RecordLiteral& record
) {
   uint32_t position_idx = 0;
   bool found_position = false;
   std::string symbol_str;
   bool found_symbol = false;

   for (const auto& field : record.fields) {
      if (field.name == "position") {
         const uint32_t pos_val = extractUint32Literal(*field.value);
         CHECK_RHYDB_QUERY(
            pos_val > 0,
            "The 'position' field in a {} MutationProfile mutation is 1-indexed; "
            "value 0 is not allowed",
            SymbolType::SYMBOL_NAME
         );
         position_idx = pos_val - 1;
         found_position = true;
      } else if (field.name == "symbol") {
         symbol_str = extractStringLiteral(*field.value);
         found_symbol = true;
      }
   }

   CHECK_RHYDB_QUERY(
      found_position,
      "Each mutation in a {} MutationProfile expression must have a 'position' field",
      SymbolType::SYMBOL_NAME
   );
   CHECK_RHYDB_QUERY(
      found_symbol,
      "Each mutation in a {} MutationProfile expression must have a 'symbol' field",
      SymbolType::SYMBOL_NAME
   );
   CHECK_RHYDB_QUERY(
      symbol_str.size() == 1,
      "The 'symbol' field in a {} MutationProfile mutation must be a single character",
      SymbolType::SYMBOL_NAME
   );
   const auto sym = SymbolType::charToSymbol(symbol_str[0]);
   CHECK_RHYDB_QUERY(
      sym.has_value(),
      "Invalid {} symbol '{}' in MutationProfile",
      SymbolType::SYMBOL_NAME,
      symbol_str[0]
   );

   return {position_idx, sym.value()};
}

template <typename SymbolType>
std::vector<typename scalar_expressions::MutationProfile<SymbolType>::Mutation> parseMutationList(
   const ast::SetLiteral& mutations_set
) {
   using MP = scalar_expressions::MutationProfile<SymbolType>;
   std::vector<typename MP::Mutation> parsed_mutations;
   for (const auto& elem : mutations_set.elements) {
      CHECK_RHYDB_QUERY(
         std::holds_alternative<ast::RecordLiteral>(elem->value),
         "Each element of 'mutations' in a {} MutationProfile expression must be a record "
         "literal with 'position' and 'symbol' fields",
         SymbolType::SYMBOL_NAME
      );
      parsed_mutations.push_back(
         parseMutationRecord<SymbolType>(std::get<ast::RecordLiteral>(elem->value))
      );
   }
   return parsed_mutations;
}

template <typename SymbolType>
ScalarExpressionPtr handleMutationProfile(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& /*tables*/
) {
   const uint32_t distance = extractUint32Literal(args.at("distance"));
   auto sequence_name = extractStringLiteral(args.at("sequenceName"));
   auto column = resolveColumn(sequence_name, schema);

   const auto* query_seq_expr = args.get("querySequence");
   const auto* sequence_id_expr = args.get("sequenceId");
   const auto* mutations_expr = args.get("mutations");
   const int input_count = static_cast<int>(query_seq_expr != nullptr) +
                           static_cast<int>(sequence_id_expr != nullptr) +
                           static_cast<int>(mutations_expr != nullptr);
   CHECK_RHYDB_QUERY(
      input_count == 1,
      "Exactly one of 'querySequence', 'sequenceId', or 'mutations' must be provided in a {} "
      "MutationProfile expression, but {} were provided",
      SymbolType::SYMBOL_NAME,
      input_count
   );

   using MP = scalar_expressions::MutationProfile<SymbolType>;

   if (query_seq_expr != nullptr) {
      return std::make_unique<MP>(
         column, distance, typename MP::QuerySequenceInput{extractStringLiteral(*query_seq_expr)}
      );
   }
   if (sequence_id_expr != nullptr) {
      return std::make_unique<MP>(
         column, distance, typename MP::SequenceIdInput{extractStringLiteral(*sequence_id_expr)}
      );
   }

   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::SetLiteral>(mutations_expr->value),
      "The 'mutations' argument of a {} MutationProfile expression must be a set literal",
      SymbolType::SYMBOL_NAME
   );
   auto parsed_mutations =
      parseMutationList<SymbolType>(std::get<ast::SetLiteral>(mutations_expr->value));
   return std::make_unique<MP>(
      std::move(column), distance, typename MP::MutationsInput{std::move(parsed_mutations)}
   );
}

ScalarExpressionPtr convertIdentifierToFilter(
   const ast::Identifier& node,
   const ast::Expression& ast,
   const std::vector<schema::ColumnIdentifier>& schema
) {
   auto found =
      std::ranges::find_if(schema, [&](const auto& col) { return col.name == node.name; });
   CHECK_RHYDB_QUERY(
      found != schema.end(),
      "filter references unknown column '{}' at {}:{}",
      node.name,
      ast.location.line,
      ast.location.column
   );
   return std::make_unique<scalar_expressions::FieldRef>(*found);
}

ScalarExpressionPtr convertFunctionCallToFilter(
   const ast::FunctionCall& node,
   const ast::Expression& ast,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   const auto* entry = ScalarFunctionRegistry::instance().findFunction(node.function_name);
   CHECK_RHYDB_QUERY(entry != nullptr, "unknown scalar function '{}'", node.function_name);
   auto bound = bindArguments(
      node.function_name, entry->signature, node.positional_arguments, node.named_arguments
   );
   auto expression = entry->handler(bound, schema, tables);
   // The registry also holds value-producing scalar functions (e.g. `at`), which are not filter
   // predicates. Reject them here rather than letting a non-boolean expression reach compile().
   CHECK_RHYDB_QUERY(
      expression->type() == schema::ColumnType::BOOL,
      "scalar function '{}' produces a {} value and cannot be used as a filter predicate at {}:{}",
      node.function_name,
      schema::columnTypeToString(expression->type()),
      ast.location.line,
      ast.location.column
   );
   return expression;
}

}  // namespace

std::unique_ptr<scalar_expressions::ScalarExpression> convertToFilter(
   const ast::Expression& ast,
   const std::vector<schema::ColumnIdentifier>& schema,
   const Tables& tables
) {
   return std::visit(
      [&](const auto& node) -> ScalarExpressionPtr {
         using T = std::decay_t<decltype(node)>;

         if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            return convertBinaryExprToFilter(node, schema, tables);
         } else if constexpr (std::is_same_v<T, ast::UnaryNotExpr>) {
            return std::make_unique<scalar_expressions::Negation>(
               convertToFilter(*node.operand, schema, tables)
            );
         } else if constexpr (std::is_same_v<T, ast::BoolLiteral>) {
            return std::make_unique<scalar_expressions::BoolLiteral>(node.value);
         } else if constexpr (std::is_same_v<T, ast::Identifier>) {
            return convertIdentifierToFilter(node, ast, schema);
         } else if constexpr (std::is_same_v<T, ast::FunctionCall>) {
            return convertFunctionCallToFilter(node, ast, schema, tables);
         } else {
            throw IllegalQueryException(
               "unsupported expression type in filter context at {}:{}",
               ast.location.line,
               ast.location.column
            );
         },
      },
      ast.value
   );
}

// ========================================================================
// Pipeline function handlers (registered in FunctionRegistry)
// ========================================================================

namespace {

operators::AggregateFunction parseAggregateFunctionName(const std::string& function_name) {
   if (function_name == "count") {
      return operators::AggregateFunction::COUNT;
   }
   if (function_name == "sum") {
      return operators::AggregateFunction::SUM;
   }
   throw IllegalQueryException(
      "unknown aggregate function '{}'. Valid functions: count, sum", function_name
   );
}

bool isNumericColumnType(schema::ColumnType type) {
   return type == schema::ColumnType::INT32 || type == schema::ColumnType::INT64 ||
          type == schema::ColumnType::FLOAT;
}

/// Checks the arguments of `sum(column)`: exactly one source column, which must be numeric
void validateSumArguments(
   const ast::RecordField& field,
   const ast::FunctionCall& func,
   const std::optional<schema::ColumnIdentifier>& source_column
) {
   CHECK_RHYDB_QUERY(
      func.positional_arguments.size() == 1 && func.named_arguments.empty(),
      "aggregate '{}': sum expects exactly one column argument, e.g. sum(age)",
      field.name
   );
   CHECK_RHYDB_QUERY(
      isNumericColumnType(source_column->type),
      "aggregate '{}': sum requires a numeric (int, int64 or float) column, but '{}' has type {}",
      field.name,
      source_column->name,
      schema::columnTypeToString(source_column->type)
   );
}

struct GroupByArgs {
   std::vector<schema::ColumnIdentifier> group_by_fields;
   std::vector<operators::AggregateDefinition> aggregates;
};

operators::AggregateDefinition parseAggregateDefinition(
   const ast::RecordField& field,
   const std::vector<schema::ColumnIdentifier>& schema
) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::FunctionCall>(field.value->value),
      "aggregate definition '{}' must be a function call (e.g. count(), sum(col))",
      field.name
   );
   const auto& func = std::get<ast::FunctionCall>(field.value->value);
   auto agg_func = parseAggregateFunctionName(func.function_name);
   std::optional<schema::ColumnIdentifier> source_column;
   if (!func.positional_arguments.empty()) {
      auto source_column_name = extractIdentifierName(*func.positional_arguments[0].value);
      auto found = std::ranges::find_if(schema, [&](const auto& col) {
         return col.name == source_column_name;
      });
      CHECK_RHYDB_QUERY(
         found != schema.end(),
         "source column {} is not present in the input's output schema",
         source_column_name
      );
      source_column = *found;
   }
   if (agg_func == operators::AggregateFunction::SUM) {
      validateSumArguments(field, func, source_column);
   }
   return {
      .output_name = field.name, .function = agg_func, .source_column = std::move(source_column),
   };
}

std::vector<schema::ColumnIdentifier> parseGroupByFields(
   const ast::SetLiteral& set,
   const std::vector<schema::ColumnIdentifier>& schema
) {
   std::vector<schema::ColumnIdentifier> group_by_fields;
   for (const auto& elem : set.elements) {
      auto group_by_name = extractIdentifierName(*elem);
      auto found =
         std::ranges::find_if(schema, [&](const auto& col) { return col.name == group_by_name; });
      CHECK_RHYDB_QUERY(
         found != schema.end(),
         "groupBy field '{}' is not present in the input's output schema",
         group_by_name
      );
      group_by_fields.push_back(*found);
   }
   return group_by_fields;
}

GroupByArgs parseGroupBySpecs(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& child_schema
) {
   GroupByArgs result;

   // Parse aggregates (required) — a RecordLiteral like {count:=count()}
   const auto& agg_expr = args.at("aggregates");
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::RecordLiteral>(agg_expr.value),
      "groupBy aggregates must be a record literal like {{count:=count()}}"
   );
   const auto& record = std::get<ast::RecordLiteral>(agg_expr.value);
   for (const auto& field : record.fields) {
      result.aggregates.push_back(parseAggregateDefinition(field, child_schema));
   }
   // Parse columns (optional) — a SetLiteral like {pango_lineage, division}
   if (const auto* columns_expr = args.get("columns")) {
      const auto& set = extractSetLiteral(*columns_expr);
      result.group_by_fields = parseGroupByFields(set, child_schema);
   }

   return result;
}

std::vector<std::string> names(const std::vector<schema::ColumnIdentifier>& schema) {
   std::vector<std::string> names;
   names.reserve(schema.size());
   for (const auto& col : schema) {
      names.push_back(col.name);
   }
   return names;
}

std::vector<std::string> namesWithTypes(const std::vector<schema::ColumnIdentifier>& schema) {
   std::vector<std::string> result;
   result.reserve(schema.size());
   for (const auto& col : schema) {
      result.push_back(fmt::format("{}:{}", col.name, schema::columnTypeToString(col.type)));
   }
   return result;
}

OrderByField parseOrderByField(
   const ast::Expression& expression,
   const std::vector<schema::ColumnIdentifier>& child_schema
) {
   if (std::holds_alternative<ast::Identifier>(expression.value)) {
      const auto identifier_name = extractIdentifierName(expression);
      auto found =
         std::ranges::find_if(child_schema.begin(), child_schema.end(), [&](const auto& col) {
            return col.name == identifier_name;
         });
      CHECK_RHYDB_QUERY(
         found != child_schema.end(),
         "OrderByField {} is not contained in the result of this operation. "
         "Allowed values are {}.",
         identifier_name,
         fmt::join(names(child_schema), ", ")
      );
      return {.field = *found, .ascending = true};
   }
   if (std::holds_alternative<ast::FunctionCall>(expression.value)) {
      const auto& call = std::get<ast::FunctionCall>(expression.value);
      CHECK_RHYDB_QUERY(
         call.function_name == "asc" || call.function_name == "desc",
         "orderBy field must be an identifier or asc()/desc() call, got '{}' at {}:{}",
         call.function_name,
         expression.location.line,
         expression.location.column
      );
      CHECK_RHYDB_QUERY(
         call.positional_arguments.size() == 1 && call.named_arguments.empty(),
         "{}() expects exactly one argument",
         call.function_name
      );
      const auto identifier_name = extractIdentifierName(*call.positional_arguments[0].value);
      auto found =
         std::ranges::find_if(child_schema.begin(), child_schema.end(), [&](const auto& col) {
            return col.name == identifier_name;
         });
      CHECK_RHYDB_QUERY(
         found != child_schema.end(),
         "OrderByField {} is not contained in the result of this operation. "
         "Allowed values are {}.",
         identifier_name,
         fmt::join(names(child_schema), ", ")
      );
      return {.field = *found, .ascending = call.function_name == "asc"};
   }
   throw IllegalQueryException(
      "orderBy field must be an identifier or asc()/desc() call at {}:{}",
      expression.location.line,
      expression.location.column
   );
}

std::vector<OrderByField> parseOrderByFields(
   const ast::Expression& expression,
   const std::vector<schema::ColumnIdentifier>& child_schema
) {
   const auto& set = extractSetLiteral(expression);
   std::vector<OrderByField> fields;
   for (const auto& elem : set.elements) {
      auto order_by_field = parseOrderByField(*elem, child_schema);
      fields.push_back(order_by_field);
   }
   return fields;
}

namespace {

/// Visitor that extracts the reference sequence / dictionary string for a sequence column.
/// Returns std::nullopt for column types that do not require decompression.
class ColumnToReferenceSequenceVisitor {
  public:
   template <Column ColumnType>
   std::optional<std::string> operator()(
      const schema::TableSchema& /*table_schema*/,
      const schema::ColumnIdentifier& /*column_identifier*/
   ) {
      return std::nullopt;
   }
};

template <>
std::optional<std::string> ColumnToReferenceSequenceVisitor::operator()<SequenceColumn<Nucleotide>>(
   const schema::TableSchema& table_schema,
   const schema::ColumnIdentifier& column_identifier
) {
   auto* metadata =
      table_schema.getColumnMetadata<SequenceColumn<Nucleotide>>(column_identifier.name).value();
   return Nucleotide::sequenceToString(metadata->reference_sequence);
}

template <>
std::optional<std::string> ColumnToReferenceSequenceVisitor::operator()<SequenceColumn<AminoAcid>>(
   const schema::TableSchema& table_schema,
   const schema::ColumnIdentifier& column_identifier
) {
   auto* metadata =
      table_schema.getColumnMetadata<SequenceColumn<AminoAcid>>(column_identifier.name).value();
   return AminoAcid::sequenceToString(metadata->reference_sequence);
}

template <>
std::optional<std::string> ColumnToReferenceSequenceVisitor::operator()<ZstdCompressedStringColumn>(
   const schema::TableSchema& table_schema,
   const schema::ColumnIdentifier& column_identifier
) {
   auto* metadata =
      table_schema.getColumnMetadata<ZstdCompressedStringColumn>(column_identifier.name).value();
   return metadata->dictionary_string;
}

operators::QueryNodePtr wrapWithDecompressIfNeeded(
   operators::QueryNodePtr node,
   const std::shared_ptr<schema::TableSchema>& table_schema
) {
   std::vector<operators::MapNode::Assignment> assignments;
   for (const auto& col : node->getOutputSchema()) {
      if (!schema::isSequenceColumn(col.type)) {
         continue;
      }
      auto reference =
         storage::column::visit(col.type, ColumnToReferenceSequenceVisitor{}, *table_schema, col);
      if (reference.has_value()) {
         assignments.push_back(
            {.output_column = {.name = col.name, .type = schema::ColumnType::STRING},
             .expression = std::make_unique<scalar_expressions::ZstdDecompressScalar>(
                std::make_unique<scalar_expressions::FieldRef>(col), std::move(reference).value()
             ),}
         );
      }
   }
   if (assignments.empty()) {
      return node;
   }
   return std::make_unique<operators::MapNode>(std::move(node), std::move(assignments));
}

}  // namespace

operators::QueryNodePtr buildScanNode(
   const ast::Expression& ast,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
) {
   const auto& name = std::get<ast::Identifier>(ast.value).name;
   auto table_name = schema::TableName(name);
   auto iter = tables.find(table_name);
   CHECK_RHYDB_QUERY(
      iter != tables.end(), "table '{}' not found in database", table_name.getName()
   );
   const auto table_schema = iter->second->schema;
   std::vector<schema::ColumnIdentifier> fields = iter->second->schema->getColumnIdentifiers();
   auto table_scan = std::make_unique<operators::TableScanNode>(
      iter->second, std::make_unique<scalar_expressions::BoolLiteral>(true), std::move(fields)
   );
   return wrapWithDecompressIfNeeded(std::move(table_scan), table_schema);
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleFilter(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   auto filter_expr = convertToFilter(args.at("predicate"), child->getOutputSchema(), tables);
   return std::make_unique<operators::FilterNode>(std::move(child), std::move(filter_expr));
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleSchema(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   return std::make_unique<operators::SchemaNode>(std::move(child));
}

operators::QueryNodePtr handleTables(
   const BoundArguments& /*args*/,
   const Tables& /*tables*/,
   const ChildConverter& /*convert_child*/
) {
   return std::make_unique<operators::TablesNode>();
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleGroupBy(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   auto child_schema = child->getOutputSchema();

   auto [group_by_fields, aggregates] = parseGroupBySpecs(args, child_schema);

   return std::make_unique<operators::AggregateNode>(
      std::move(child), std::move(group_by_fields), std::move(aggregates)
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleProject(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   const auto& field_argument = args.at("fields");
   const std::vector<std::string> field_names =
      holds_alternative<ast::Identifier>(field_argument.value)
         ? std::vector{extractIdentifierName(field_argument)}
         : extractSetOfIdentifiers(field_argument);
   auto child = convert_child(args.at("input"), tables);
   auto child_schema = child->getOutputSchema();
   std::vector<schema::ColumnIdentifier> fields;
   for (const auto& name : field_names) {
      auto found =
         std::ranges::find_if(child_schema, [&](const auto& col) { return col.name == name; });
      CHECK_RHYDB_QUERY(
         found != child_schema.end(),
         "project field '{}' is not present in the input's output schema",
         name
      );
      fields.emplace_back(name, found->type);
   }
   return std::make_unique<operators::ProjectNode>(std::move(child), std::move(fields));
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleProjectout(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   const auto& field_argument = args.at("fields");
   const std::vector<std::string> remove_names =
      holds_alternative<ast::Identifier>(field_argument.value)
         ? std::vector{extractIdentifierName(field_argument)}
         : extractSetOfIdentifiers(field_argument);
   auto child = convert_child(args.at("input"), tables);
   auto child_schema = child->getOutputSchema();
   for (const auto& name : remove_names) {
      auto found =
         std::ranges::find_if(child_schema, [&](const auto& col) { return col.name == name; });
      CHECK_RHYDB_QUERY(
         found != child_schema.end(),
         "projectout field '{}' is not present in the input's output schema",
         name
      );
   }
   const std::unordered_set<std::string> remove_set(remove_names.begin(), remove_names.end());
   std::vector<schema::ColumnIdentifier> fields;
   for (const auto& col : child_schema) {
      if (!remove_set.contains(col.name)) {
         fields.push_back(col);
      }
   }
   return std::make_unique<operators::ProjectNode>(std::move(child), std::move(fields));
}

namespace {

using operators::MapNode;

/// Parses a single `name := value` assignment of a map() record. The assigned
/// value may be any scalar expression (see convertToScalar); the output column's
/// type is taken from that expression.
MapNode::Assignment parseMapAssignment(
   const ast::RecordField& field,
   const std::vector<schema::ColumnIdentifier>& child_schema,
   const Tables& tables
) {
   auto expression = convertToScalar(
      *field.value, child_schema, fmt::format("map() field '{}'", field.name), tables
   );
   return {
      .output_column = {.name = field.name, .type = expression->type()},
      .expression = std::move(expression),
   };
}

}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleMap(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   const auto& expressions_argument = args.at("expressions");
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::RecordLiteral>(expressions_argument.value),
      "map() expects a record of assignments like {x := 3, y := age}"
   );
   const auto& record = std::get<ast::RecordLiteral>(expressions_argument.value);
   CHECK_RHYDB_QUERY(!record.fields.empty(), "map() requires at least one assignment");

   auto child = convert_child(args.at("input"), tables);
   const auto child_schema = child->getOutputSchema();

   std::unordered_set<std::string> seen_output_names;
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.reserve(record.fields.size());
   for (const auto& field : record.fields) {
      CHECK_RHYDB_QUERY(
         seen_output_names.insert(field.name).second,
         "map() assigns the output column '{}' more than once",
         field.name
      );
      assignments.push_back(parseMapAssignment(field, child_schema, tables));
   }

   return std::make_unique<operators::MapNode>(std::move(child), std::move(assignments));
}

namespace {
constexpr std::string_view NUCLEOTIDE_MUTATIONS_FUNCTION_NAME = "mutations";
constexpr std::string_view AMINO_ACID_MUTATIONS_FUNCTION_NAME = "aminoAcidMutations";
}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleMutations(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   std::vector<std::string> sequence_names;
   if (const auto* seq_expr = args.get("sequenceNames")) {
      sequence_names = extractSetOfIdentifiers(*seq_expr);
   }
   auto min_proportion = extractNumericAsFloatLiteral(args.at("minProportion"));
   CHECK_RHYDB_QUERY(
      min_proportion >= 0 && min_proportion <= 1,
      "Invalid proportion: minProportion must be in interval [0.0, 1.0]"
   );
   std::vector<std::string> field_strings;
   if (const auto* expr = args.get("fields")) {
      field_strings = extractSetOfIdentifiers(*expr);
   }
   if (args.functionName() == NUCLEOTIDE_MUTATIONS_FUNCTION_NAME) {
      return std::make_unique<operators::UnresolvedMutationsNode<Nucleotide>>(
         std::move(child), std::move(sequence_names), min_proportion, std::move(field_strings)
      );
   }
   return std::make_unique<operators::UnresolvedMutationsNode<AminoAcid>>(
      std::move(child), std::move(sequence_names), min_proportion, std::move(field_strings)
   );
}

namespace {
constexpr std::string_view NUCLEOTIDE_INSERTIONS_FUNCTION_NAME = "insertions";
constexpr std::string_view AMINO_ACID_INSERTIONS_FUNCTION_NAME = "aminoAcidInsertions";
}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleInsertions(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   std::vector<std::string> sequence_names;
   if (const auto* seq_expr = args.get("sequenceNames")) {
      sequence_names = extractSetOfIdentifiers(*seq_expr);
   }
   if (args.functionName() == NUCLEOTIDE_INSERTIONS_FUNCTION_NAME) {
      return std::make_unique<operators::UnresolvedInsertionsNode<Nucleotide>>(
         std::move(child), std::move(sequence_names)
      );
   }
   return std::make_unique<operators::UnresolvedInsertionsNode<AminoAcid>>(
      std::move(child), std::move(sequence_names)
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleRandomize(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   uint32_t seed;
   auto seed_arg = args.getOptionalUint32("seed");
   if (seed_arg.has_value()) {
      seed = seed_arg.value();
   } else {
      seed = static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
   }
   return std::make_unique<operators::OrderByNode>(
      std::move(child), std::vector<OrderByField>{}, seed
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleLimit(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   const uint32_t limit_val = extractUint32Literal(args.at("count"));
   CHECK_RHYDB_QUERY(limit_val > 0, "limit must be a positive number");
   auto offset = args.getOptionalUint32("offset");
   return std::make_unique<operators::FetchNode>(std::move(child), limit_val, offset);
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleOffset(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   uint32_t offset_val = extractUint32Literal(args.at("count"));
   return std::make_unique<operators::FetchNode>(std::move(child), std::nullopt, offset_val);
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleOrderBy(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   auto order_fields = parseOrderByFields(args.at("fields"), child->getOutputSchema());
   return std::make_unique<operators::OrderByNode>(
      std::move(child), std::move(order_fields), std::nullopt
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleMostRecentCommonAncestor(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto column_name = extractStringLiteral(args.at("column"));
   bool print_nodes_not_in_tree = false;
   if (const auto* expr = args.get("printNodesNotInTree")) {
      print_nodes_not_in_tree = extractBoolLiteral(*expr);
   }
   auto child = convert_child(args.at("input"), tables);
   return std::make_unique<operators::UnresolvedMostRecentCommonAncestorNode>(
      std::move(child), std::move(column_name), print_nodes_not_in_tree
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handlePhyloSubtree(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto column_name = extractStringLiteral(args.at("column"));
   bool print_nodes_not_in_tree = false;
   bool contract_unary_nodes = true;
   if (const auto* expr = args.get("printNodesNotInTree")) {
      print_nodes_not_in_tree = extractBoolLiteral(*expr);
   }
   if (const auto* expr = args.get("contractUnaryNodes")) {
      contract_unary_nodes = extractBoolLiteral(*expr);
   }
   auto child = convert_child(args.at("input"), tables);
   return std::make_unique<operators::UnresolvedPhyloSubtreeNode>(
      std::move(child), std::move(column_name), print_nodes_not_in_tree, contract_unary_nodes
   );
}

namespace {

std::optional<schema::ColumnIdentifier> findColumnByName(
   const std::vector<schema::ColumnIdentifier>& schema,
   const std::string& name
) {
   auto found = std::ranges::find_if(schema, [&](const auto& col) { return col.name == name; });
   if (found == schema.end()) {
      return std::nullopt;
   }
   return *found;
}

struct JoinKeys {
   std::vector<schema::ColumnIdentifier> left;
   std::vector<schema::ColumnIdentifier> right;
};

/// Resolves an identifier appearing in a join condition against exactly one of the two
/// join inputs. Errors if the name is present on neither side or both sides
enum class JoinSide : uint8_t { LEFT, RIGHT };

struct ResolvedJoinColumn {
   JoinSide side;
   schema::ColumnIdentifier column;
};

ResolvedJoinColumn resolveJoinColumn(
   const ast::Expression& expression,
   const std::vector<schema::ColumnIdentifier>& left_schema,
   const std::vector<schema::ColumnIdentifier>& right_schema
) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::Identifier>(expression.value),
      "join() on-expression must compare column identifiers, got '{}' at {}:{}",
      expression.toString(),
      expression.location.line,
      expression.location.column
   );
   const auto name = extractIdentifierName(expression);
   auto in_left = findColumnByName(left_schema, name);
   auto in_right = findColumnByName(right_schema, name);
   CHECK_RHYDB_QUERY(
      !(in_left.has_value() && in_right.has_value()),
      "join() on-expression references column '{}', which exists in both inputs and is therefore "
      "ambiguous. Rename one side (e.g. via map()) before joining.",
      name
   );
   CHECK_RHYDB_QUERY(
      in_left.has_value() || in_right.has_value(),
      "join() on-expression references unknown column '{}'",
      name
   );
   if (in_left.has_value()) {
      return {.side = JoinSide::LEFT, .column = in_left.value()};
   }
   return {.side = JoinSide::RIGHT, .column = in_right.value()};
}

// NOLINTNEXTLINE(misc-no-recursion)
void collectJoinKeys(
   const ast::Expression& on_expression,
   const std::vector<schema::ColumnIdentifier>& left_schema,
   const std::vector<schema::ColumnIdentifier>& right_schema,
   JoinKeys& keys
) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::BinaryExpr>(on_expression.value),
      "join() on-expression must be an equality between a left and a right column, or a "
      "conjunction (&&) of such equalities, at {}:{}",
      on_expression.location.line,
      on_expression.location.column
   );
   const auto& binary = std::get<ast::BinaryExpr>(on_expression.value);
   if (binary.op == BinaryOp::AND) {
      collectJoinKeys(*binary.left, left_schema, right_schema, keys);
      collectJoinKeys(*binary.right, left_schema, right_schema, keys);
      return;
   }
   CHECK_RHYDB_QUERY(
      binary.op == BinaryOp::EQUALS,
      "join() on-expression only supports equality (=) comparisons, optionally combined with "
      "'&&', at {}:{}",
      on_expression.location.line,
      on_expression.location.column
   );
   auto first = resolveJoinColumn(*binary.left, left_schema, right_schema);
   auto second = resolveJoinColumn(*binary.right, left_schema, right_schema);
   CHECK_RHYDB_QUERY(
      first.side != second.side,
      "join() on-expression equality must reference one column from each input, but both '{}' and "
      "'{}' resolve to the same input at {}",
      binary.left->toString(),
      binary.right->toString(),
      on_expression.location.toString()
   );
   CHECK_RHYDB_QUERY(
      exec_node::columnTypeToArrowType(first.column.type)
         ->Equals(*exec_node::columnTypeToArrowType(second.column.type)),
      "join() on-expression equality must reference equal column types from each input, but "
      "'{}' and '{}' have mismatching types {} and {} at {}",
      binary.left->toString(),
      binary.right->toString(),
      schema::columnTypeToString(first.column.type),
      schema::columnTypeToString(second.column.type),
      on_expression.location.toString()
   );
   if (first.side == JoinSide::LEFT) {
      keys.left.push_back(first.column);
      keys.right.push_back(second.column);
   } else {
      keys.left.push_back(second.column);
      keys.right.push_back(first.column);
   }
}

arrow::acero::JoinType parseJoinType(const BoundArguments& args) {
   if (!args.has("type")) {
      return arrow::acero::JoinType::INNER;
   }
   const auto* type_expr = args.get("type");
   const auto name = extractIdentifierName(*type_expr);
   if (name == "inner") {
      return arrow::acero::JoinType::INNER;
   }
   if (name == "left") {
      return arrow::acero::JoinType::LEFT_OUTER;
   }
   if (name == "right") {
      return arrow::acero::JoinType::RIGHT_OUTER;
   }
   if (name == "full") {
      return arrow::acero::JoinType::FULL_OUTER;
   }
   if (name == "leftSemi") {
      return arrow::acero::JoinType::LEFT_SEMI;
   }
   if (name == "rightSemi") {
      return arrow::acero::JoinType::RIGHT_SEMI;
   }
   if (name == "leftAnti") {
      return arrow::acero::JoinType::LEFT_ANTI;
   }
   if (name == "rightAnti") {
      return arrow::acero::JoinType::RIGHT_ANTI;
   }
   throw IllegalQueryException(
      "invalid join type '{}'. Valid types are: inner, left, right, full, leftSemi, rightSemi, "
      "leftAnti, rightAnti",
      name
   );
}

}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleJoin(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto left = convert_child(args.at("left"), tables);
   auto right = convert_child(args.at("right"), tables);

   auto left_schema = left->getOutputSchema();
   auto right_schema = right->getOutputSchema();

   // A join concatenates the columns of both inputs, so a name occurring on both sides would
   // produce a result with duplicate column names and make the on-expression ambiguous.
   std::vector<std::string> overlapping_names;
   for (const auto& left_column : left_schema) {
      if (findColumnByName(right_schema, left_column.name).has_value()) {
         overlapping_names.push_back(left_column.name);
      }
   }
   CHECK_RHYDB_QUERY(
      overlapping_names.empty(),
      "join() requires the two inputs to have disjoint column names, but the column(s) [{}] are "
      "present in both. Rename one side (e.g. via map()) before joining.",
      fmt::join(overlapping_names, ", ")
   );

   JoinKeys keys;
   collectJoinKeys(args.at("on"), left_schema, right_schema, keys);
   CHECK_RHYDB_QUERY(
      !keys.left.empty(),
      "join() on-expression must contain at least one equality between a left and a right column"
   );

   const auto join_type = parseJoinType(args);

   return std::make_unique<operators::JoinNode>(
      std::move(left), std::move(right), std::move(keys.left), std::move(keys.right), join_type
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleUnionAll(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto left = convert_child(args.at("left"), tables);
   auto right = convert_child(args.at("right"), tables);

   auto left_schema = left->getOutputSchema();
   auto right_schema = right->getOutputSchema();
   CHECK_RHYDB_QUERY(
      left_schema == right_schema,
      "unionAll requires both inputs to have the same schema "
      "(same column names, types, and order). "
      "Left schema: [{}], right schema: [{}].",
      fmt::join(namesWithTypes(left_schema), ", "),
      fmt::join(namesWithTypes(right_schema), ", ")
   );

   return std::make_unique<operators::UnionAllNode>(std::move(left), std::move(right));
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr handleTransitiveClosure(
   const BoundArguments& args,
   const Tables& tables,
   const ChildConverter& convert_child
) {
   auto child = convert_child(args.at("input"), tables);
   auto from_column = extractStringLiteral(args.at("from"));
   auto to_column = extractStringLiteral(args.at("to"));
   bool include_vertices = false;
   if (const auto* expr = args.get("includeVertices")) {
      include_vertices = extractBoolLiteral(*expr);
   }
   std::optional<std::vector<std::string>> starting_from;
   if (const auto* expr = args.get("startingFrom")) {
      const auto& set = extractSetLiteral(*expr);
      starting_from.emplace();
      starting_from->reserve(set.elements.size());
      for (const auto& element : set.elements) {
         starting_from->push_back(extractStringLiteral(*element));
      }
   }

   return std::make_unique<operators::TransitiveClosureNode>(
      std::move(child),
      std::move(from_column),
      std::move(to_column),
      include_vertices,
      std::move(starting_from)
   );
}

}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr convertExpression(
   const ast::Expression& ast,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
) {
   if (std::holds_alternative<ast::Identifier>(ast.value)) {
      return buildScanNode(ast, tables);
   }

   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::FunctionCall>(ast.value),
      "expected table reference or function call at {}:{}",
      ast.location.line,
      ast.location.column
   );
   const auto& call = std::get<ast::FunctionCall>(ast.value);

   const auto* entry = FunctionRegistry::instance().findFunction(call.function_name);
   CHECK_RHYDB_QUERY(
      entry != nullptr,
      "unknown function '{}' at {}:{}",
      call.function_name,
      ast.location.line,
      ast.location.column
   );

   auto bound = bindArguments(
      call.function_name, entry->signature, call.positional_arguments, call.named_arguments
   );
   return entry->handler(bound, tables, convertExpression);
}

// ========================================================================
// Public entry point
// ========================================================================

operators::QueryNodePtr convertToQueryTree(
   const ast::Expression& ast,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
) {
   return convertExpression(ast, tables);
}

operators::QueryNodePtr parseAndConvertToQueryTree(
   std::string_view query_string,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
) {
   Parser parser(query_string);
   auto ast = parser.parse();
   return convertToQueryTree(*ast, tables);
}

// ========================================================================
// Registry construction — signatures + handlers
// ========================================================================

// Shorthand helpers for building signatures
namespace {
ParameterDefinition pos(std::string name, bool required = true) {
   return {.name = std::move(name), .required = required, .positional = true};
}
ParameterDefinition named(std::string name, bool required = true) {
   return {.name = std::move(name), .required = required, .positional = false};
}
}  // namespace

FunctionRegistry::FunctionRegistry() {
   registerFunction("filter", {{pos("input"), pos("predicate")}}, handleFilter);

   registerFunction("schema", {{pos("input")}}, handleSchema);

   registerFunction("tables", {{}}, handleTables);

   registerFunction(
      "groupBy", {{pos("input"), pos("aggregates"), pos("columns", false)}}, handleGroupBy
   );

   registerFunction("project", {{pos("input"), pos("fields")}}, handleProject);

   registerFunction("projectout", {{pos("input"), pos("fields")}}, handleProjectout);

   registerFunction("map", {{pos("input"), pos("expressions")}}, handleMap);

   auto mutations_sig = FunctionSignature{
      {pos("input"), named("minProportion"), named("sequenceNames", false), named("fields", false)},
   };
   registerFunction(
      std::string{NUCLEOTIDE_MUTATIONS_FUNCTION_NAME}, mutations_sig, handleMutations
   );
   registerFunction(
      std::string{AMINO_ACID_MUTATIONS_FUNCTION_NAME}, mutations_sig, handleMutations
   );

   auto insertions_sig = FunctionSignature{{pos("input"), named("sequenceNames", false)}};
   registerFunction(
      std::string{NUCLEOTIDE_INSERTIONS_FUNCTION_NAME}, insertions_sig, handleInsertions
   );
   registerFunction(
      std::string{AMINO_ACID_INSERTIONS_FUNCTION_NAME}, insertions_sig, handleInsertions
   );

   registerFunction("randomize", {{pos("input"), named("seed", false)}}, handleRandomize);

   registerFunction("limit", {{pos("input"), pos("count")}}, handleLimit);

   registerFunction("offset", {{pos("input"), pos("count")}}, handleOffset);

   registerFunction("orderBy", {{pos("input"), pos("fields")}}, handleOrderBy);

   registerFunction(
      "mostRecentCommonAncestor",
      {{pos("input"), pos("column"), named("printNodesNotInTree", false)}},
      handleMostRecentCommonAncestor
   );

   registerFunction(
      "phyloSubtree",
      {{pos("input"),
        pos("column"),
        named("printNodesNotInTree", false),
        named("contractUnaryNodes", false),},},
      handlePhyloSubtree
   );

   registerFunction("unionAll", {{pos("left"), pos("right")}}, handleUnionAll);

   registerFunction(
      "join", {{pos("left"), pos("right"), pos("on"), named("type", false)}}, handleJoin
   );

   registerFunction(
      "transitiveClosure",
      {{pos("input"),
        pos("from"),
        pos("to"),
        named("includeVertices", false),
        named("startingFrom", false),},},
      handleTransitiveClosure
   );
}

FunctionRegistry& FunctionRegistry::instance() {
   static FunctionRegistry registry;
   return registry;
}

ScalarFunctionRegistry::ScalarFunctionRegistry() {
   registerFunction("between", {{pos("column"), pos("from"), pos("to")}}, handleBetween);

   registerFunction("in", {{pos("column"), pos("values")}}, handleIn);

   registerFunction("isNull", {{pos("column")}}, handleIsNull);
   registerFunction("isNotNull", {{pos("column")}}, handleIsNotNull);

   registerFunction(
      "lineage",
      {{pos("column"),
        pos("value"),
        named("includeSublineages", false),
        named("recombinantFollowingMode", false),},},
      handleLineage
   );

   registerFunction("phyloDescendantOf", {{pos("column"), pos("node")}}, handlePhyloDescendantOf);

   registerFunction("like", {{pos("column"), pos("pattern")}}, handleLike);

   registerFunction("at", {{pos("input"), pos("position")}}, handleAt);
   registerFunction("isoWeek", {{pos("input")}}, handleIsoWeek);

   auto symbol_equals_sig =
      FunctionSignature{{named("position"), named("symbol"), named("sequenceName")}};
   registerFunction("nucleotideEquals", symbol_equals_sig, handleSymbolEquals<Nucleotide>);
   registerFunction("aminoAcidEquals", symbol_equals_sig, handleSymbolEquals<AminoAcid>);

   auto has_mutation_sig = FunctionSignature{{named("position"), named("sequenceName")}};
   registerFunction("hasMutation", has_mutation_sig, handleHasMutation<Nucleotide>);
   registerFunction("hasAAMutation", has_mutation_sig, handleHasMutation<AminoAcid>);

   auto insertion_contains_sig =
      FunctionSignature{{named("position"), named("value"), named("sequenceName")}};
   registerFunction("insertionContains", insertion_contains_sig, handleInsertionContains<Nucleotide>);
   registerFunction("aminoAcidInsertionContains", insertion_contains_sig, handleInsertionContains<AminoAcid>);

   registerFunction("exact", {{pos("child")}}, handleExact);
   registerFunction("maybe", {{pos("child")}}, handleMaybe);

   registerFunction(
      "nOf", {{pos("count"), pos("children"), named("matchExactly", false)}}, handleNOf
   );

   auto mutation_profile_sig = FunctionSignature{{
      named("distance"),
      named("sequenceName"),
      named("querySequence", false),
      named("sequenceId", false),
      named("mutations", false),
   },};
   registerFunction("nucleotideMutationProfile", mutation_profile_sig, handleMutationProfile<Nucleotide>);
   registerFunction("aminoAcidMutationProfile", mutation_profile_sig, handleMutationProfile<AminoAcid>);
}

ScalarFunctionRegistry& ScalarFunctionRegistry::instance() {
   static ScalarFunctionRegistry registry;
   return registry;
}

}  // namespace rhydb::query_engine::saneql
