#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/command/write_command.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb {
class Database;
}

namespace rhydb::query_engine::command {

/// One column of a `createTable` statement as written in the query. Sequence columns only name the
/// row of the built-in `reference_genomes` table that holds their reference; its sequence is looked
/// up when the command executes.
struct ColumnDefinition {
   std::string name;
   schema::ColumnType type;
   /// The `name` of the `reference_genomes` row whose sequence is the column's reference sequence
   /// (for aligned sequence columns) or compression dictionary (for unaligned sequence columns).
   /// Unset for value columns.
   std::optional<std::string> reference_name;
};

/// Builds the command of a `createTable` statement from its bound `table`, `columns` and
/// `primaryKey` arguments.
[[nodiscard]] WriteCommandPtr buildCreateTable(
   const saneql::BoundArguments& args,
   const saneql::Tables& tables,
   const saneql::ChildConverter& convert_child
);

/// `createTable(<table>, {<column> := <type>, ...}, primaryKey := <column>)`: creates a new, empty
/// table with the given schema.
class CreateTableCommand : public WriteCommand {
   schema::TableName table_name_;
   std::vector<ColumnDefinition> columns_;
   std::optional<std::string> primary_key_;

  public:
   /// Expects `primary_key`, if set, to name a `string` column of `columns` (checked by
   /// `buildCreateTable`); the references of sequence columns are only resolved in `execute`.
   CreateTableCommand(
      schema::TableName table_name,
      std::vector<ColumnDefinition> columns,
      std::optional<std::string> primary_key
   );

   [[nodiscard]] nlohmann::json execute(
      Database& database,
      const config::QueryOptions& query_options,
      std::string_view request_id
   ) override;
};

}  // namespace rhydb::query_engine::command
