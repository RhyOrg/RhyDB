#include "rhydb/query_engine/saneql/table_literal_builder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <arrow/array/array_base.h>
#include <arrow/array/builder_base.h>
#include <arrow/builder.h>
#include <arrow/compute/api.h>
#include <arrow/scalar.h>
#include <arrow/table.h>
#include <arrow/type.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/table_literal_node.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::saneql {

namespace {

// Building a table() literal goes through arrow::Result; this raises its failures as query errors.
template <typename T>
T tableCellOrThrow(arrow::Result<T> result) {
   CHECK_RHYDB_QUERY(result.ok(), "table() could not be built: {}", result.status().ToString());
   return std::move(result).ValueOrDie();
}

/// Evaluates a constant scalar expression (a cell of a table() literal) to a single Arrow scalar.
std::shared_ptr<arrow::Scalar> evaluateConstantCell(
   const scalar_expressions::ScalarExpression& value
) {
   const auto expression =
      tableCellOrThrow(tableCellOrThrow(value.toArrowExpression()).Bind(arrow::Schema{{}}));
   const auto datum = tableCellOrThrow(
      arrow::compute::ExecuteScalarExpression(expression, arrow::ExecBatch{{}, 1})
   );
   if (datum.is_scalar()) {
      return datum.scalar();
   }
   return tableCellOrThrow(datum.make_array()->GetScalar(0));
}

/// The cell expressions of a single column of a table() literal, kept in row order. A null entry is
/// the SaneQL `null` literal, whose type is only known once the whole column is seen.
struct TableLiteralColumn {
   std::string name;
   std::vector<std::shared_ptr<arrow::Scalar>> cells;  // nullptr marks a `null` cell
   std::optional<schema::DataType> type;               // the type of the non-null cells
};

/// Converts one cell expression, enforcing that it is a constant (no column references). A `null`
/// cell yields a nullptr scalar whose column type is resolved later.
void addCell(TableLiteralColumn& column, const ast::Expression& cell, const Tables& tables) {
   if (ast::isNullLiteral(cell)) {
      column.cells.push_back(nullptr);
      return;
   }
   const auto error_context = fmt::format("table() cell for column '{}'", column.name);
   auto expression = convertToScalar(cell, {}, error_context, tables);
   const auto cell_type = expression->type();
   CHECK_RHYDB_QUERY(
      !column.type.has_value() || *column.type == cell_type,
      "table() column '{}' mixes values of type '{}' and '{}' at {}:{}; every value in a column "
      "must have the same type",
      column.name,
      schema::dataTypeToString(*column.type),
      schema::dataTypeToString(cell_type),
      cell.location.line,
      cell.location.column
   );
   column.type = cell_type;
   column.cells.push_back(evaluateConstantCell(*expression));
}

/// Builds the Arrow array of a fully-collected column, filling `null` cells with a typed null.
std::shared_ptr<arrow::Array> buildTableLiteralColumnArray(const TableLiteralColumn& column) {
   CHECK_RHYDB_QUERY(
      column.type.has_value(),
      "table() column '{}' has only null values, so its type cannot be inferred; give at least one "
      "non-null value in the column",
      column.name
   );
   const auto arrow_type = exec_node::columnTypeToArrowType(*column.type);
   const auto null_scalar = arrow::MakeNullScalar(arrow_type);

   arrow::ScalarVector scalars;
   scalars.reserve(column.cells.size());
   for (const auto& cell : column.cells) {
      scalars.push_back(cell == nullptr ? null_scalar : cell);
   }

   auto builder = tableCellOrThrow(arrow::MakeBuilder(arrow_type));
   const auto append_status = builder->AppendScalars(scalars);
   CHECK_RHYDB_QUERY(
      append_status.ok(),
      "table() could not build column '{}': {}",
      column.name,
      append_status.ToString()
   );
   return tableCellOrThrow(builder->Finish());
}

/// The rows of a table() literal: a non-empty set whose first element names the columns.
const std::vector<ast::ExpressionPtr>& tableRowsOrThrow(const ast::Expression& rows_argument) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::SetLiteral>(rows_argument.value),
      "table() expects a set of rows like {{a:=1,b:=2}},{{3,4}} at {}:{}",
      rows_argument.location.line,
      rows_argument.location.column
   );
   const auto& rows = std::get<ast::SetLiteral>(rows_argument.value).elements;
   CHECK_RHYDB_QUERY(
      !rows.empty(),
      "table() requires at least one row at {}:{}",
      rows_argument.location.line,
      rows_argument.location.column
   );
   return rows;
}

/// The first row names the columns and fixes their count and order.
const ast::RecordLiteral& tableHeaderOrThrow(const ast::Expression& first_row) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::RecordLiteral>(first_row.value),
      "table()'s first row must name its columns like {{a:=1,b:=2}} at {}:{}",
      first_row.location.line,
      first_row.location.column
   );
   const auto& header = std::get<ast::RecordLiteral>(first_row.value);
   CHECK_RHYDB_QUERY(
      !header.fields.empty(),
      "table()'s first row must have at least one column at {}:{}",
      first_row.location.line,
      first_row.location.column
   );
   return header;
}

/// One empty column per header field; column names must be unique.
std::vector<TableLiteralColumn> makeTableColumns(
   const ast::RecordLiteral& header,
   const ast::Expression& first_row
) {
   std::vector<TableLiteralColumn> columns;
   columns.reserve(header.fields.size());
   std::unordered_set<std::string> seen_names;
   for (const auto& field : header.fields) {
      CHECK_RHYDB_QUERY(
         seen_names.insert(field.name).second,
         "table() names the column '{}' more than once at {}:{}",
         field.name,
         first_row.location.line,
         first_row.location.column
      );
      columns.push_back({.name = field.name, .cells = {}, .type = std::nullopt});
   }
   return columns;
}

/// Every row after the first is positional and must have as many values as there are columns.
void addPositionalRow(
   std::vector<TableLiteralColumn>& columns,
   const ast::Expression& row,
   const Tables& tables
) {
   CHECK_RHYDB_QUERY(
      std::holds_alternative<ast::SetLiteral>(row.value),
      "table() rows after the first must be positional like {{3,4}} at {}:{}",
      row.location.line,
      row.location.column
   );
   const auto& values = std::get<ast::SetLiteral>(row.value).elements;
   CHECK_RHYDB_QUERY(
      values.size() == columns.size(),
      "table() row has {} values but the table has {} columns at {}:{}",
      values.size(),
      columns.size(),
      row.location.line,
      row.location.column
   );
   for (size_t value_index = 0; value_index < values.size(); ++value_index) {
      addCell(columns.at(value_index), *values.at(value_index), tables);
   }
}

operators::QueryNodePtr makeTableLiteralNode(
   const std::vector<TableLiteralColumn>& columns,
   size_t row_count
) {
   std::vector<schema::ColumnIdentifier> output_schema;
   output_schema.reserve(columns.size());
   arrow::ArrayVector arrays;
   arrays.reserve(columns.size());
   for (const auto& column : columns) {
      arrays.push_back(buildTableLiteralColumnArray(column));
      output_schema.push_back({.name = column.name, .type = *column.type});
   }

   const auto arrow_schema = exec_node::columnsToArrowSchema(output_schema);
   auto table = arrow::Table::Make(arrow_schema, arrays, static_cast<int64_t>(row_count));
   return std::make_unique<operators::TableLiteralNode>(std::move(output_schema), std::move(table));
}

}  // namespace

operators::QueryNodePtr buildTableLiteralNode(const BoundArguments& args, const Tables& tables) {
   const auto& rows = tableRowsOrThrow(args.at("rows"));
   const auto& first_row = *rows.front();
   const auto& header = tableHeaderOrThrow(first_row);
   auto columns = makeTableColumns(header, first_row);

   for (size_t field_index = 0; field_index < header.fields.size(); ++field_index) {
      addCell(columns.at(field_index), *header.fields.at(field_index).value, tables);
   }
   for (size_t row_index = 1; row_index < rows.size(); ++row_index) {
      addPositionalRow(columns, *rows.at(row_index), tables);
   }
   return makeTableLiteralNode(columns, rows.size());
}

}  // namespace rhydb::query_engine::saneql
