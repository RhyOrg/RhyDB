#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/command/write_command.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb {
class Database;
}

namespace rhydb::query_engine::command {

/// One column of a `createTable` statement as written in the query.
struct ColumnDefinition {
   std::string name;
   schema::ColumnType type;
   /// The `name` of the `reference_genomes` row whose sequence is the column's reference sequence.
   /// Only set for aligned sequence columns.
   std::optional<std::string> reference_name;
   /// The query that computes the compression dictionary. Only set for `zstdCompressedString`
   /// columns.
   std::optional<operators::QueryNodePtr> dictionary_query;
};

/// Builds the command of a `createTable` statement from its bound `table`, `columns` and
/// `primaryKey` arguments.
[[nodiscard]] WriteCommandPtr buildCreateTable(
   const saneql::BoundArguments& args,
   const saneql::Tables& tables,
   const saneql::ChildConverter& convert_child
);

/// `createTable(<table>, {<column> := <type>, ...}, primaryKey := <column>)`: creates a new, empty
/// table with the given schema. The definition is validated, and references and dictionaries are
/// resolved, in `execute`.
class CreateTableCommand : public WriteCommand {
   schema::TableName table_name_;
   std::vector<ColumnDefinition> columns_;
   std::optional<std::string> primary_key_;

  public:
   CreateTableCommand(
      schema::TableName table_name,
      std::vector<ColumnDefinition> columns,
      std::optional<std::string> primary_key
   );

   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Table>> execute(
      Database& database,
      const config::QueryOptions& query_options,
      std::string_view request_id
   ) override;
};

}  // namespace rhydb::query_engine::command
