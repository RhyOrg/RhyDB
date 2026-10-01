#pragma once

#include <memory>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/command/write_command.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb {
class Database;
}

namespace rhydb::query_engine::command {

/// Builds the command of a `createTable` statement from its bound `table`, `columns` and
/// `primaryKey` arguments. Validates the primary key and resolves the references of sequence
/// columns against the `reference_genomes` table in `tables`.
[[nodiscard]] WriteCommandPtr buildCreateTable(
   const saneql::BoundArguments& args,
   const saneql::Tables& tables,
   const saneql::ChildConverter& convert_child
);

/// `createTable(<table>, {<column> := <type>, ...}, primaryKey := <column>)`: creates a new, empty
/// table with the given schema.
class CreateTableCommand : public WriteCommand {
   schema::TableName table_name_;
   std::shared_ptr<schema::TableSchema> table_schema_;

  public:
   CreateTableCommand(
      schema::TableName table_name,
      std::shared_ptr<schema::TableSchema> table_schema
   );

   [[nodiscard]] nlohmann::json execute(
      Database& database,
      const config::QueryOptions& query_options,
      std::string_view request_id
   ) override;
};

}  // namespace rhydb::query_engine::command
