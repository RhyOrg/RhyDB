#include "lineage_definition_handler.h"

#include <optional>
#include <string>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <nlohmann/json.hpp>
#include <utility>

#include <rhydb/query_engine/illegal_query_exception.h>
#include <rhydb/schema/builtin_tables.h>
#include <rhydb/storage/column/string_column.h>

#include "active_database.h"
#include "bad_request.h"
#include "error_request_handler.h"

namespace rhydb_app {

using rhydb::schema::DataType;

namespace {

/// The lineage definition file of the lineage column `column_name`, as preprocessing stored it in
/// the built-in lineage definitions table.
std::optional<std::string> findLineageDefinition(
   const rhydb::Database& database,
   const std::string& column_name
) {
   const auto table = database.tables.find(
      rhydb::schema::TableName{std::string{rhydb::schema::LINEAGE_DEFINITIONS_TABLE_NAME}}
   );
   if (table == database.tables.end()) {
      return std::nullopt;
   }
   const auto& column_name_column = table->second->getColumn<rhydb::storage::column::StringColumn>(
      std::string{rhydb::schema::LINEAGE_DEFINITIONS_COLUMN_NAME_COLUMN}
   );
   const auto& definition_column = table->second->getColumn<rhydb::storage::column::StringColumn>(
      std::string{rhydb::schema::LINEAGE_DEFINITIONS_DEFINITION_COLUMN}
   );
   for (const auto row_id : table->second->row_layout) {
      if (column_name_column.isNull(row_id) || definition_column.isNull(row_id)) {
         continue;
      }
      if (column_name_column.getValueString(row_id) == column_name) {
         return definition_column.getValueString(row_id);
      }
   }
   return std::nullopt;
}

}  // namespace

LineageDefinitionHandler::LineageDefinitionHandler(
   std::shared_ptr<ActiveDatabase> database_handle,
   std::string column_name
)
    : database_handle(std::move(database_handle)),
      column_name(std::move(column_name)) {}

void LineageDefinitionHandler::get(
   Poco::Net::HTTPServerRequest& /*request*/,
   Poco::Net::HTTPServerResponse& response
) {
   const auto database = database_handle->getActiveDatabase();

   response.set("data-version", database->getDataVersionTimestamp().value);

   const rhydb::schema::TableName& table_name = rhydb::schema::TableName::getDefault();

   auto table = database->tables.find(table_name);
   if (table == database->tables.end()) {
      throw BadRequest("The database does not contain a table with name {}", table_name.getName());
   }

   auto column_identifier = table->second->schema->getColumn(column_name);
   if (column_identifier == std::nullopt) {
      throw BadRequest("The column {} does not exist in this instance.", column_name);
   }
   if (column_identifier.value().type != DataType::dictionaryEncoded(DataType::STRING)) {
      throw BadRequest("The column {} is not of type dictionary-encoded string.", column_name);
   }

   // Resolved before the response is sent: sending commits the 200, after which a BadRequest
   // could no longer set the status.
   const auto lineage_definition = findLineageDefinition(*database, column_name);
   if (!lineage_definition.has_value()) {
      throw BadRequest("The column {} does not have a lineageIndex defined.", column_name);
   }
   const std::string& lineage_definition_yaml = lineage_definition.value();

   response.setContentType("application/yaml");
   response.sendBuffer(lineage_definition_yaml.data(), lineage_definition_yaml.size());
}
}  // namespace rhydb_app
