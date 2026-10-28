#include "rhydb/query_engine/command/update_command.h"

#include <memory>
#include <set>
#include <utility>
#include <variant>

#include <arrow/scalar.h>

#include "rhydb/database.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::command {

namespace ast = saneql::ast;

WriteCommandPtr buildUpdate(const saneql::BoundArguments& args, const saneql::Tables& tables) {
   auto table_name = schema::TableName(ast::extractIdentifierName(args.at("table")));
   const auto table = tables.find(table_name);
   CHECK_RHYDB_QUERY(
      table != tables.end(), "update() table '{}' not found in database", table_name.getName()
   );

   auto predicate = buildWherePredicate(args, table->second, tables);

   const auto* set = std::get_if<ast::RecordLiteral>(&args.at("set").value);
   CHECK_RHYDB_QUERY(
      set != nullptr && !set->fields.empty(),
      "update() expects `set` to be a record of the columns to assign, e.g. `{{age := 42}}`, but "
      "got '{}'",
      args.at("set").toString()
   );
   std::vector<ColumnAssignment> assignments;
   std::set<std::string> assigned_columns;
   for (const auto& [column_name, value] : set->fields) {
      const auto column = table->second->schema->getColumn(column_name);
      CHECK_RHYDB_QUERY(
         column.has_value(),
         "The table '{}' does not contain a column '{}'",
         table_name.getName(),
         column_name
      );
      CHECK_RHYDB_QUERY(
         assigned_columns.insert(column_name).second,
         "update() assigns the column '{}' more than once",
         column_name
      );
      assignments.push_back(makeColumnAssignment(table->second, *column, *value, tables));
   }

   return std::make_unique<UpdateCommand>(
      std::move(table_name), std::move(predicate), std::move(assignments)
   );
}

UpdateCommand::UpdateCommand(
   schema::TableName table_name,
   std::unique_ptr<scalar_expressions::ScalarExpression> predicate,
   std::vector<ColumnAssignment> assignments
)
    : table_name_(std::move(table_name)),
      predicate_(std::move(predicate)),
      assignments_(std::move(assignments)) {}

// Not thread-safe: the columns are updated in place, so the update must not run concurrently with
// other queries against the same table.
arrow::Result<std::shared_ptr<arrow::Table>> UpdateCommand::execute(
   Database& database,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   // The rows are selected and the values computed before anything is assigned, so the predicate
   // and the values see the table from before the update even when they read an updated column.
   const size_t updated_rows = applyColumnAssignments(
      database.tables.at(table_name_),
      assignments_,
      *predicate_,
      database.tables,
      query_options,
      request_id
   );

   database.updateDataVersion();

   return makeWriteSummary("updatedRows", arrow::MakeScalar(static_cast<int64_t>(updated_rows)));
}

}  // namespace rhydb::query_engine::command
