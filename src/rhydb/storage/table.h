#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <string>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/bool_column.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/float_column.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/column/row_layout.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

namespace rhydb::storage {

class TableChunkBuilder;

class Table {
  public:
   schema::TableName table_name;
   std::shared_ptr<schema::TableSchema> schema;

   std::map<std::string, column::StringColumn> string_columns;
   std::map<std::string, column::DictionaryEncodedColumn> dictionary_encoded_columns;
   std::map<std::string, column::BoolColumn> bool_columns;
   std::map<std::string, column::Int32Column> int32_columns;
   std::map<std::string, column::Int64Column> int64_columns;
   std::map<std::string, column::FloatColumn> float_columns;
   std::map<std::string, column::Date32Column> date32_columns;
   std::map<std::string, column::SequenceColumn<Nucleotide>> nuc_columns;
   std::map<std::string, column::SequenceColumn<AminoAcid>> aa_columns;
   std::map<std::string, column::ZstdCompressedStringColumn> zstd_compressed_string_columns;

   uint32_t row_count = 0;
   /// The shared per-chunk row layout of this table partition: every column is appended to in
   /// lockstep, so this single layout is the source of truth for iterating the partition's rows by
   /// `RowId`. `row_count == row_layout.numRows()`.
   column::RowLayout row_layout;

   explicit Table(schema::TableName table_name, std::shared_ptr<schema::TableSchema> schema);

   Table(Table&& other) = default;
   Table& operator=(Table&& other) = default;

   Table(const Table& other) = delete;
   Table& operator=(const Table& other) = delete;

   template <class Archive>
   void serializeData(Archive& archive, [[maybe_unused]] const uint32_t version) {
      // clang-format off
      for (auto& [name, store] : string_columns) {
         archive & store;
      }
      for (auto& [name, store] : dictionary_encoded_columns) {
         archive & store;
      }
      for (auto& [name, store] : bool_columns) {
         archive & store;
      }
      for (auto& [name, store] : int32_columns) {
         archive & store;
      }
      for (auto& [name, store] : float_columns) {
         archive & store;
      }
      for (auto& [name, store] : date32_columns) {
         archive & store;
      }
      for (auto& [name, store] : int64_columns) {
         archive & store;
      }
      for (auto& [name, store] : nuc_columns) {
         archive & store;
      }
      for (auto& [name, store] : aa_columns) {
         archive & store;
      }
      for (auto& [name, store] : zstd_compressed_string_columns) {
         archive & store;
      }
      archive & row_count;
      archive & row_layout;
      // clang-format on
   }

   template <column::Column ColumnType>
   std::map<std::string, ColumnType>& getColumns();

   template <column::Column ColumnType>
   [[nodiscard]] const std::map<std::string, ColumnType>& getColumns() const;

   template <column::Column ColumnType>
   [[nodiscard]] bool hasColumn(const std::string& name) const {
      return getColumns<ColumnType>().contains(name);
   }

   template <column::Column ColumnType>
   ColumnType& getColumn(const std::string& name) {
      return getColumns<ColumnType>().at(name);
   }

   template <column::Column ColumnType>
   [[nodiscard]] const ColumnType& getColumn(const std::string& name) const {
      return getColumns<ColumnType>().at(name);
   }

   [[nodiscard]] nlohmann::json logTable() const;

   void validate() const;

   /// Apply a finalized ingestion chunk (one buffer per column) to the columns'
   /// global structures. Consumes (clears) the builder's buffers.
   std::expected<void, std::string> bulkInsert(TableChunkBuilder& block);

   void finalize();

   void loadData(const std::filesystem::path& path);
   void saveData(const std::filesystem::path& path);
   void validatePrimaryKeyUnique() const;

  private:
   void validateNucleotideSequences() const;
   void validateAminoAcidSequences() const;
   void validateMetadataColumns() const;

   template <typename Column>
   void validateColumnsHaveSize(
      const std::map<std::string, Column>& columnsOfTheType,
      const std::string& columnType
   ) const;
};

}  // namespace rhydb::storage
