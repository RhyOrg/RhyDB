#pragma once

#include <expected>
#include <filesystem>
#include <string_view>

#include <arrow/table.h>

#include "rhydb/append/table_inserter.h"
#include "rhydb/common/data_version.h"
#include "rhydb/common/rhydb_directory.h"
#include "rhydb/config/runtime_config.h"
#include "rhydb/database_info.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb {

class Database {
  public:
   schema::DatabaseSchema schema;
   std::map<schema::TableName, std::shared_ptr<storage::Table>> tables;

   void updateDataVersion();

  private:
   DataVersion data_version_ = DataVersion::mineDataVersion();

   void createMissingBuiltinTables();

  public:
   Database();

   explicit Database(schema::DatabaseSchema database_schema);

   virtual ~Database() = default;

   /// Checks that `table_name` can name a table. A table's data is saved to a file named after it,
   /// so the name may only contain letters, digits, '_' and '-' (no path separators or dots), and
   /// must not collide with the files that hold the database's own metadata.
   [[nodiscard]] static std::expected<void, std::string> validateTableName(
      std::string_view table_name
   );

   /// Throws if `table_name` is invalid, see `validateTableName`.
   void createTable(
      schema::TableName table_name,
      std::shared_ptr<schema::TableSchema> table_schema
   );

   void appendData(
      const schema::TableName& table_name,
      std::istream& input_stream,
      append::ClusteredBufferingOptions clustering_options = {}
   );

   void createNucleotideSequenceTable(
      const std::string& table_name,
      const std::string& primary_key_name,
      const std::string& sequence_name,
      const std::string& reference_sequence,
      const std::vector<std::string>& extra_string_columns = {}
   );

   void createGeneTable(
      const std::string& table_name,
      const std::string& primary_key_name,
      const std::string& sequence_name,
      const std::string& reference_sequence,
      const std::vector<std::string>& extra_string_columns = {}
   );

   void appendDataFromFile(const std::string& table_name, const std::string& file_path);

   void appendDataFromString(const std::string& table_name, std::string json_string);

   void printAllData(const std::string& table_name) const;

   std::string getNucleotideReferenceSequence(
      const std::string& table_name,
      const std::string& sequence_name
   );

   std::string getAminoAcidReferenceSequence(
      const std::string& table_name,
      const std::string& sequence_name
   );

   roaring::Roaring getFilteredBitmap(const std::string& table_name, const std::string& filter);

   /// Assigns the scalar `value` to the column `column_name` of `table_name` for every row matched
   /// by the SaneQL `filter_expression`. `value` is a single SaneQL literal (parsed by the same
   /// lexer/parser as queries) matching the column's type, e.g. `3`, `3.14`, `true`, or
   /// `'2021-03-15'::date`; the literal `null` clears the matched rows. Only scalar value columns
   /// (INT32, FLOAT, DATE32, BOOL) can be updated; other column types raise an error.
   void updateColumn(
      const std::string& table_name,
      const std::string& column_name,
      const std::string& value,
      const std::string& filter_expression
   );

   void saveDatabaseState(const std::filesystem::path& save_directory);

   static std::optional<Database> loadDatabaseStateFromPath(
      const std::filesystem::path& save_directory
   );

   static Database loadDatabaseState(const RhyDBDataSource& rhydb_data_source);

   [[nodiscard]] virtual DatabaseInfo getDatabaseInfo() const;

   [[nodiscard]] virtual DataVersion::Timestamp getDataVersionTimestamp() const;

   [[nodiscard]] std::string executeQueryAsArrowIpc(const std::string& query_string) const;

   /// Applies the write statement `query_string` and returns its result (see
   /// `WriteCommand::execute`).
   std::shared_ptr<arrow::Table> executeWrite(
      const std::string& query_string,
      const config::QueryOptions& query_options,
      std::string_view request_id
   );
};

}  // namespace rhydb
