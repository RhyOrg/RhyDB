#include "rhydb/database.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/writer.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "rhydb/append/table_inserter.h"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/data_version.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/rhydb_directory.h"
#include "rhydb/common/version.h"
#include "rhydb/database_info.h"
#include "rhydb/persistence/exception.h"
#include "rhydb/query_engine/command/write_command.h"
#include "rhydb/query_engine/exec_node/arrow_ipc_sink.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/query_engine/saneql/parser.h"
#include "rhydb/query_engine/scalar_column_update.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"

namespace {
template <typename SymbolType>
std::optional<std::vector<typename SymbolType::Symbol>> stringToSymbolVector(
   const std::string& sequence
) {
   const size_t size = sequence.size();
   std::vector<typename SymbolType::Symbol> result;
   result.reserve(size);
   for (size_t i = 0; i < size; ++i) {
      if (i + 1 < size && sequence[i] == '\\') {
         ++i;
      }
      auto symbol = SymbolType::charToSymbol(sequence[i]);
      if (symbol == std::nullopt) {
         return std::nullopt;
      }
      result.emplace_back(symbol.value());
   }
   return result;
}

template <typename SymbolType>
std::string symbolVectorToString(const std::vector<typename SymbolType::Symbol>& sequence) {
   const size_t size = sequence.size();
   std::string result;
   result.reserve(size);
   for (const auto& symbol : sequence) {
      auto character = SymbolType::symbolToChar(symbol);
      result += character;
   }
   return result;
}
}  // namespace

namespace rhydb {

Database::Database(schema::DatabaseSchema database_schema)
    : schema(std::move(database_schema)) {
   for (const auto& [table_name, table_schema] : schema.tables) {
      tables.emplace(table_name, std::make_shared<storage::Table>(table_name, table_schema));
   }
}

void Database::createTable(
   schema::TableName table_name,
   std::shared_ptr<schema::TableSchema> table_schema
) {
   tables.emplace(table_name, std::make_shared<storage::Table>(table_name, table_schema));
   schema.tables.emplace(std::move(table_name), std::move(table_schema));
}

void Database::appendData(
   const schema::TableName& table_name,
   std::istream& input_stream,
   append::ClusteredBufferingOptions clustering_options
) {
   rhydb::append::NdjsonLineReader input_data{input_stream};
   RHYDB_ASSERT(tables.contains(table_name));
   auto& table = tables.at(table_name);
   rhydb::append::appendDataToTable(table, input_data, std::move(clustering_options));
   updateDataVersion();
   SPDLOG_INFO("Database info: {}", getDatabaseInfo());
}

void Database::createNucleotideSequenceTable(
   const std::string& table_name,
   const std::string& primary_key_name,
   const std::string& sequence_name,
   const std::string& reference_sequence,
   const std::vector<std::string>& extra_string_columns
) {
   auto table_schema = std::make_shared<schema::TableSchema>();
   const schema::ColumnIdentifier primary_key = {
      .name = primary_key_name, .type = schema::ColumnType::STRING,
   };
   table_schema->column_metadata.emplace(
      primary_key, std::make_shared<storage::column::StringColumnMetadata>(primary_key_name)
   );
   auto reference_sequence_vector = stringToSymbolVector<Nucleotide>(reference_sequence).value();
   table_schema->column_metadata.emplace(
      schema::ColumnIdentifier{
         .name = sequence_name, .type = schema::ColumnType::NUCLEOTIDE_SEQUENCE,
      },
      std::make_shared<storage::column::SequenceColumnMetadata<Nucleotide>>(
         sequence_name, std::move(reference_sequence_vector)
      )
   );
   for (const auto& column_name : extra_string_columns) {
      table_schema->column_metadata.emplace(
         schema::ColumnIdentifier{.name = column_name, .type = schema::ColumnType::STRING},
         std::make_shared<storage::column::StringColumnMetadata>(column_name)
      );
   }
   table_schema->primary_key = primary_key;
   createTable(schema::TableName(table_name), std::move(table_schema));
}

void Database::createGeneTable(
   const std::string& table_name,
   const std::string& primary_key_name,
   const std::string& sequence_name,
   const std::string& reference_sequence,
   const std::vector<std::string>& extra_string_columns
) {
   auto table_schema = std::make_shared<schema::TableSchema>();
   const schema::ColumnIdentifier primary_key = {
      .name = primary_key_name, .type = schema::ColumnType::STRING,
   };
   table_schema->column_metadata.emplace(
      primary_key, std::make_shared<storage::column::StringColumnMetadata>(primary_key_name)
   );
   auto reference_sequence_vector = stringToSymbolVector<AminoAcid>(reference_sequence).value();
   table_schema->column_metadata.emplace(
      schema::ColumnIdentifier{
         .name = sequence_name, .type = schema::ColumnType::AMINO_ACID_SEQUENCE,
      },
      std::make_shared<storage::column::SequenceColumnMetadata<AminoAcid>>(
         sequence_name, std::move(reference_sequence_vector)
      )
   );
   for (const auto& column_name : extra_string_columns) {
      table_schema->column_metadata.emplace(
         schema::ColumnIdentifier{.name = column_name, .type = schema::ColumnType::STRING},
         std::make_shared<storage::column::StringColumnMetadata>(column_name)
      );
   }
   table_schema->primary_key = primary_key;
   createTable(schema::TableName(table_name), std::move(table_schema));
}

void Database::appendDataFromFile(const std::string& table_name, const std::string& file_path) {
   std::ifstream input_stream(file_path);
   rhydb::append::NdjsonLineReader input_data{input_stream};
   rhydb::append::appendDataToTable(tables.at(schema::TableName{table_name}), input_data);
   SPDLOG_INFO("Database info: {}", getDatabaseInfo());
}

void Database::appendDataFromString(const std::string& table_name, std::string json_string) {
   std::stringstream input_stream(std::move(json_string));
   rhydb::append::NdjsonLineReader input_data{input_stream};
   rhydb::append::appendDataToTable(tables.at(schema::TableName{table_name}), input_data);
}

using rhydb::query_engine::scalar_expressions::BoolLiteral;
using rhydb::query_engine::scalar_expressions::ScalarExpression;

void Database::printAllData(const std::string& table_name) const {
   auto table_iter = tables.find(schema::TableName{table_name});
   if (table_iter == tables.end()) {
      throw std::runtime_error{fmt::format("The database does not contain table {}", table_name)};
   }
   auto table = table_iter->second;

   std::vector<schema::ColumnIdentifier> all_columns = table->schema->getColumnIdentifiers();

   auto query_node = std::make_unique<query_engine::operators::TableScanNode>(
      table, std::make_unique<BoolLiteral>(true), std::move(all_columns)
   );

   auto query_plan = query_engine::Planner::planQuery(
      std::move(query_node), tables, config::QueryOptions{}, "printAllData"
   );
   query_engine::exec_node::NdjsonSink output_sink{&std::cout, query_plan.results_schema};
   query_plan.executeAndWrite(output_sink, /*timeout_in_seconds=*/100);
}

std::string Database::getNucleotideReferenceSequence(
   const std::string& table_name,
   const std::string& sequence_name
) {
   auto maybe_table_schema = schema.tables.find(schema::TableName{table_name});
   if (maybe_table_schema == schema.tables.end()) {
      throw std::runtime_error{fmt::format("The database does not contain table {}", table_name)};
   }
   const auto& table_schema = maybe_table_schema->second;

   auto maybe_sequence_column_metadata =
      table_schema->getColumnMetadata<storage::column::SequenceColumn<Nucleotide>>(sequence_name);
   if (maybe_sequence_column_metadata == std::nullopt) {
      SPDLOG_ERROR(
         "The database table {} does not contain the nucleotide sequence column {}",
         table_name,
         sequence_name
      );
      return {};
   }

   const auto& sequence_column_metadata = maybe_sequence_column_metadata.value();

   return symbolVectorToString<Nucleotide>(sequence_column_metadata->reference_sequence);
}

std::string Database::getAminoAcidReferenceSequence(
   const std::string& table_name,
   const std::string& sequence_name
) {
   auto maybe_table_schema = schema.tables.find(schema::TableName{table_name});
   if (maybe_table_schema == schema.tables.end()) {
      throw std::runtime_error{fmt::format("The database does not contain table {}", table_name)};
   }
   const auto& table_schema = maybe_table_schema->second;

   auto maybe_sequence_column_metadata =
      table_schema->getColumnMetadata<storage::column::SequenceColumn<AminoAcid>>(sequence_name);
   if (maybe_sequence_column_metadata == std::nullopt) {
      SPDLOG_ERROR(
         "The database table {} does not contain the amino acid sequence column {}",
         table_name,
         sequence_name
      );
      return {};
   }

   const auto& sequence_column_metadata = maybe_sequence_column_metadata.value();

   return symbolVectorToString<AminoAcid>(sequence_column_metadata->reference_sequence);
}

roaring::Roaring Database::getFilteredBitmap(
   const std::string& table_name,
   const std::string& filter
) {
   auto maybe_table = tables.find(schema::TableName{table_name});
   if (maybe_table == tables.end()) {
      SPDLOG_ERROR("The database does not contain the table {}", table_name);
      return {};
   }
   auto table = maybe_table->second;

   query_engine::saneql::Parser parser(filter);
   auto ast = parser.parse();
   auto filter_expression =
      query_engine::saneql::convertToFilter(*ast, table->schema->getColumnIdentifiers(), tables);

   auto rewritten_filter_expression =
      filter_expression->rewrite(*table, ScalarExpression::AmbiguityMode::NONE);
   auto filter_operator = rewritten_filter_expression->compile(*table);
   roaring::Roaring bitmap = filter_operator->evaluate().toRoaring();
   return bitmap;
}

// Currently updates are not thread-safe. An update during concurrent access is not allowed
void Database::updateColumn(
   const std::string& table_name,
   const std::string& column_name,
   const std::string& value,
   const std::string& filter_expression
) {
   auto maybe_table = tables.find(schema::TableName{table_name});
   if (maybe_table == tables.end()) {
      throw query_engine::IllegalQueryException(
         fmt::format("The database does not contain the table '{}'", table_name)
      );
   }
   auto& table = *maybe_table->second;

   const auto column = table.schema->getColumn(column_name);
   if (!column.has_value()) {
      throw query_engine::IllegalQueryException(
         fmt::format("The table '{}' does not contain a column '{}'", table_name, column_name)
      );
   }

   const roaring::Roaring row_ids = getFilteredBitmap(table_name, filter_expression);
   query_engine::assignScalarLiteralToColumn(table.columns, *column, value, row_ids);

   // The update mutates persisted table data, so bump the data version like appendData does; this
   // keeps getDataVersionTimestamp() and versioned save directories consistent with the change.
   updateDataVersion();
}

namespace {

void addTableStatisticsToDatabaseInfo(DatabaseInfo& database_info, const storage::Table& table) {
   // TODO(#743) try to analyze size accuracy relative to RSS
   for (const auto& [_, seq_column] : table.columns.nuc_columns) {
      auto info = seq_column.getInfo();
      database_info.vertical_bitmaps_size += info.vertical_bitmaps_size;
      database_info.horizontal_bitmaps_size += info.horizontal_bitmaps_size;
   }
   for (const auto& [_, seq_column] : table.columns.aa_columns) {
      auto info = seq_column.getInfo();
      database_info.vertical_bitmaps_size += info.vertical_bitmaps_size;
      database_info.horizontal_bitmaps_size += info.horizontal_bitmaps_size;
   }
   database_info.sequence_count += table.row_layout.numRows();
}

}  // namespace

DatabaseInfo Database::getDatabaseInfo() const {
   DatabaseInfo database_info{
      .version = rhydb::RELEASE_VERSION,
      .sequence_count = 0,
      .vertical_bitmaps_size = 0,
      .horizontal_bitmaps_size = 0,
   };
   const auto default_table = tables.find(schema::TableName::getDefault());
   if (default_table != tables.end()) {
      addTableStatisticsToDatabaseInfo(database_info, *default_table->second);
   }
   return database_info;
}

const std::string DATABASE_SCHEMA_FILENAME = "database_schema.silo";
const std::string DATA_VERSION_FILENAME = "data_version.silo";

void Database::saveDatabaseState(const std::filesystem::path& save_directory) {
   if (getDataVersionTimestamp().value.empty()) {
      throw persistence::SaveDatabaseException(
         "Corrupted database (Data version is empty). Cannot save database."
      );
   }

   std::filesystem::create_directory(save_directory);
   if (!std::filesystem::exists(save_directory)) {
      auto error = fmt::format(
         "Could not create the directory '{}' which contains the saved databases outputs",
         save_directory.string()
      );
      SPDLOG_ERROR(error);
      throw persistence::SaveDatabaseException(error);
   }

   const std::filesystem::path versioned_save_directory =
      save_directory / getDataVersionTimestamp().value;
   SPDLOG_INFO("Saving database to '{}'", versioned_save_directory.string());

   if (std::filesystem::exists(versioned_save_directory)) {
      auto error = fmt::format(
         "In the output directory {} there already exists a file/folder with the name equal to "
         "the current data-version: {}",
         save_directory.string(),
         getDataVersionTimestamp().value
      );
      SPDLOG_ERROR(error);
      throw persistence::SaveDatabaseException(error);
   }

   std::filesystem::create_directory(versioned_save_directory);

   SPDLOG_INFO("Saving database schema");

   const auto database_schema_path = versioned_save_directory / DATABASE_SCHEMA_FILENAME;
   schema.saveToFile(database_schema_path);

   for (const auto& [table_name, table] : tables) {
      SPDLOG_DEBUG("Saving table data for table {}", table_name.getName());
      const std::filesystem::path table_file =
         versioned_save_directory / (table_name.getName() + ".silo");
      table->saveData(table_file);
   }

   data_version_.saveToFile(versioned_save_directory / DATA_VERSION_FILENAME);
}

namespace {
DataVersion loadDataVersion(const std::filesystem::path& file_path) {
   if (!std::filesystem::is_regular_file(file_path)) {
      auto error = fmt::format("Input file {} could not be opened.", file_path.string());
      throw persistence::LoadDatabaseException(error);
   }
   auto data_version = DataVersion::fromFile(file_path);
   if (data_version == std::nullopt) {
      auto error_message = fmt::format(
         "Data version file {} did not contain a valid data version", file_path.string()
      );
      SPDLOG_ERROR(error_message);
      throw persistence::LoadDatabaseException(error_message);
   }
   return data_version.value();
}
}  // namespace

std::optional<Database> Database::loadDatabaseStateFromPath(
   const std::filesystem::path& save_directory
) {
   const RhyDBDirectory rhydb_directory{save_directory};
   auto rhydb_data_source = rhydb_directory.getMostRecentDataDirectory();
   if (rhydb_data_source.has_value()) {
      return loadDatabaseState(rhydb_data_source.value());
   }
   return std::nullopt;
}

Database Database::loadDatabaseState(const RhyDBDataSource& rhydb_data_source) {
   SPDLOG_INFO("Loading database from data source: {}", rhydb_data_source.toDebugString());
   const auto save_directory = rhydb_data_source.path;

   const auto database_schema_path = save_directory / DATABASE_SCHEMA_FILENAME;
   auto schema = schema::DatabaseSchema::loadFromFile(database_schema_path);

   Database database{schema};

   for (const auto& [table_name, _] : schema.tables) {
      SPDLOG_DEBUG("Loading data for table {}", table_name.getName());
      database.tables.at(table_name)->loadData(save_directory / (table_name.getName() + ".silo"));
   }

   database.data_version_ = loadDataVersion(save_directory / DATA_VERSION_FILENAME);

   SPDLOG_INFO(
      "Finished loading data_version from {}", (save_directory / DATA_VERSION_FILENAME).string()
   );

   SPDLOG_INFO("Database info after loading: {}", database.getDatabaseInfo());

   return database;
}

DataVersion::Timestamp Database::getDataVersionTimestamp() const {
   return data_version_.timestamp;
}

void Database::updateDataVersion() {
   data_version_ = DataVersion::mineDataVersionAfter(data_version_);
   SPDLOG_DEBUG("Data version was set to {}", data_version_.toString());
}

std::string Database::executeQueryAsArrowIpc(const std::string& query_string) const {
   auto query_plan = query_engine::Planner::planSaneqlQuery(
      query_string, tables, config::QueryOptions{}, "executeQueryAsArrowIpc"
   );

   constexpr uint64_t DEFAULT_TIMEOUT_SECONDS = 120;
   std::ostringstream output_stream;
   auto output_sink =
      query_engine::exec_node::ArrowIpcSink::make(&output_stream, query_plan.results_schema);
   if (!output_sink.status().ok()) {
      throw std::runtime_error(
         fmt::format("Failed to create Arrow IPC writer: {}", output_sink.status().message())
      );
   }
   query_plan.executeAndWrite(output_sink.ValueUnsafe(), DEFAULT_TIMEOUT_SECONDS);
   return output_stream.str();
}

nlohmann::json Database::executeWrite(
   const std::string& query_string,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   const auto request = query_engine::command::parseRequest(query_string, tables);
   const auto* command = std::get_if<query_engine::command::WriteCommandPtr>(&request);
   if (command == nullptr) {
      throw query_engine::IllegalQueryException(
         "expected a write statement, e.g. `<query>.insertInto(<table>)`"
      );
   }
   return (*command)->execute(*this, query_options, request_id);
}

std::string Database::getTablesAsArrowIpc() const {
   std::string result;
   auto status = getTablesAsArrowIpcImpl().Value(&result);
   if (!status.ok()) {
      throw std::runtime_error(
         fmt::format("Failed to write finish ArrowIpcSink: {}", status.message())
      );
   }
   return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<std::string> Database::getTablesAsArrowIpcImpl() const {
   // Create schema with a single "table_name" column
   auto arrow_schema = arrow::schema({arrow::field("table_name", arrow::utf8())});

   // Build string array with table names
   arrow::StringBuilder builder;
   for (const auto& [table_name, _] : tables) {
      ARROW_RETURN_NOT_OK(builder.Append(table_name.getName()));
   }

   ARROW_ASSIGN_OR_RAISE(auto array, builder.Finish());

   ARROW_ASSIGN_OR_RAISE(auto exec_batch, arrow::ExecBatch::Make({array}, array->length()));

   std::ostringstream output_stream;
   ARROW_ASSIGN_OR_RAISE(
      auto output_sink, query_engine::exec_node::ArrowIpcSink::make(&output_stream, arrow_schema)
   );

   ARROW_RETURN_NOT_OK(output_sink.writeBatch(exec_batch));
   ARROW_RETURN_NOT_OK(output_sink.finish());
   return output_stream.str();
}

}  // namespace rhydb
