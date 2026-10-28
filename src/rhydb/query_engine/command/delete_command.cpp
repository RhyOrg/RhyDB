#include "rhydb/query_engine/command/delete_command.h"

#include <cstdint>
#include <utility>

#include <arrow/scalar.h>

#include "rhydb/common/bitmap.h"
#include "rhydb/database.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::command {

WriteCommandPtr buildDelete(const saneql::BoundArguments& args, const saneql::Tables& tables) {
   auto table_name = schema::TableName(saneql::ast::extractIdentifierName(args.at("table")));
   const auto table = tables.find(table_name);
   CHECK_RHYDB_QUERY(
      table != tables.end(), "delete() table '{}' not found in database", table_name.getName()
   );
   auto predicate = buildWherePredicate(args, table->second, tables);
   return std::make_unique<DeleteCommand>(std::move(table_name), std::move(predicate));
}

DeleteCommand::DeleteCommand(
   schema::TableName table_name,
   std::unique_ptr<scalar_expressions::ScalarExpression> predicate
)
    : table_name_(std::move(table_name)),
      predicate_(std::move(predicate)) {}

// Not thread-safe: the rows are removed in place, so the deletion must not run concurrently with
// other queries against the same table.
arrow::Result<std::shared_ptr<arrow::Table>> DeleteCommand::execute(
   Database& database,
   const config::QueryOptions& /*query_options*/,
   std::string_view /*request_id*/
) {
   const auto& table = database.tables.at(table_name_);
   const Bitmap row_ids =
      predicate_->rewrite(*table, scalar_expressions::ScalarExpression::AmbiguityMode::NONE)
         ->compile(*table)
         ->evaluate();

   // `row_ids` may view into the columns that the deletion changes, so it is not used afterwards
   const auto num_deleted_rows = static_cast<int64_t>(row_ids.cardinality());
   table->deleteRows(row_ids);
   database.updateDataVersion();

   return makeWriteSummary("deletedRows", arrow::MakeScalar(num_deleted_rows));
}

}  // namespace rhydb::query_engine::command
