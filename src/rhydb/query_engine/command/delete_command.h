#pragma once

#include <memory>
#include <string_view>

#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/command/write_command.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb {
class Database;
}

namespace rhydb::query_engine::command {

/// Builds the command of a `delete` statement from its bound `table` and `where` arguments.
[[nodiscard]] WriteCommandPtr buildDelete(
   const saneql::BoundArguments& args,
   const saneql::Tables& tables
);

/// `delete(<table>, where := <predicate>)`, modelled after SQL's `DELETE FROM <table> WHERE
/// <predicate>`: removes every row of the table matched by the predicate, or every row without
/// one. The rows are physically removed from all columns and the remaining rows are renumbered
/// (see `storage::column::RowCompaction`). The rows are selected before anything is removed, so a
/// failing predicate leaves the table unchanged.
class DeleteCommand : public WriteCommand {
   schema::TableName table_name_;
   std::unique_ptr<scalar_expressions::ScalarExpression> predicate_;

  public:
   DeleteCommand(
      schema::TableName table_name,
      std::unique_ptr<scalar_expressions::ScalarExpression> predicate
   );

   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Table>> execute(
      Database& database,
      const config::QueryOptions& query_options,
      std::string_view request_id
   ) override;
};

}  // namespace rhydb::query_engine::command
