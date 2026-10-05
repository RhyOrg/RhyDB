#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <variant>

#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column_variant.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::storage {

class TableChunkBuilder;

class Table {
  public:
   schema::TableName table_name;
   std::shared_ptr<schema::TableSchema> schema;

   std::map<std::string, column::ColumnVariant> columns;

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
      // The constructor already created every column (with the right alternative) from the
      // schema, so only the column contents are (de)serialized.
      // clang-format off
      for (auto& [name, column] : columns) {
         std::visit([&](auto& store) { archive & store; }, column);
      }
      archive & row_count;
      archive & row_layout;
      // clang-format on
   }

   template <column::Column ColumnType>
   [[nodiscard]] bool hasColumn(const std::string& name) const {
      auto column = columns.find(name);
      return column != columns.end() && std::holds_alternative<ColumnType>(column->second);
   }

   template <column::Column ColumnType>
   ColumnType& getColumn(const std::string& name) {
      return std::get<ColumnType>(columns.at(name));
   }

   template <column::Column ColumnType>
   [[nodiscard]] const ColumnType& getColumn(const std::string& name) const {
      return std::get<ColumnType>(columns.at(name));
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
   void validateColumnHasSize(const std::string& name, const Column& column) const;
};

}  // namespace rhydb::storage
