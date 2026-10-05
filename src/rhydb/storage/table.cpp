#include "rhydb/storage/table.h"

#include <fstream>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/detail/interface_iarchive.hpp>
#include <boost/archive/detail/interface_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/persistence/exception.h"
#include "rhydb/preprocessing/preprocessing_exception.h"
#include "rhydb/roaring_util/roaring_serialize.h"
#include "rhydb/schema/duplicate_primary_key_exception.h"
#include "rhydb/storage/column/column_type_visitor.h"
#include "rhydb/storage/table_chunk_builder.h"

namespace rhydb::storage {

using schema::ColumnIdentifier;
using schema::TableSchema;

using column::BoolColumn;
using column::Date32Column;
using column::DictionaryEncodedColumn;
using column::FloatColumn;
using column::Int32Column;
using column::Int64Column;
using column::SequenceColumn;
using column::StringColumn;
using column::ZstdCompressedStringColumn;

template <>
std::map<std::string, DictionaryEncodedColumn>& Table::getColumns<DictionaryEncodedColumn>() {
   return dictionary_encoded_columns;
}

template <>
std::map<std::string, StringColumn>& Table::getColumns<StringColumn>() {
   return string_columns;
}

template <>
std::map<std::string, Int32Column>& Table::getColumns<Int32Column>() {
   return int32_columns;
}

template <>
std::map<std::string, Int64Column>& Table::getColumns<Int64Column>() {
   return int64_columns;
}

template <>
std::map<std::string, BoolColumn>& Table::getColumns<BoolColumn>() {
   return bool_columns;
}

template <>
std::map<std::string, FloatColumn>& Table::getColumns<FloatColumn>() {
   return float_columns;
}

template <>
std::map<std::string, Date32Column>& Table::getColumns<Date32Column>() {
   return date32_columns;
}

template <>
std::map<std::string, SequenceColumn<Nucleotide>>& Table::getColumns<SequenceColumn<Nucleotide>>() {
   return nuc_columns;
}

template <>
std::map<std::string, SequenceColumn<AminoAcid>>& Table::getColumns<SequenceColumn<AminoAcid>>() {
   return aa_columns;
}

template <>
std::map<std::string, ZstdCompressedStringColumn>& Table::getColumns<ZstdCompressedStringColumn>() {
   return zstd_compressed_string_columns;
}

template <>
const std::map<std::string, DictionaryEncodedColumn>& Table::getColumns<DictionaryEncodedColumn>(
) const {
   return dictionary_encoded_columns;
}

template <>
const std::map<std::string, StringColumn>& Table::getColumns<StringColumn>() const {
   return string_columns;
}

template <>
const std::map<std::string, Int32Column>& Table::getColumns<Int32Column>() const {
   return int32_columns;
}

template <>
const std::map<std::string, Int64Column>& Table::getColumns<Int64Column>() const {
   return int64_columns;
}

template <>
const std::map<std::string, BoolColumn>& Table::getColumns<BoolColumn>() const {
   return bool_columns;
}

template <>
const std::map<std::string, FloatColumn>& Table::getColumns<FloatColumn>() const {
   return float_columns;
}

template <>
const std::map<std::string, Date32Column>& Table::getColumns<Date32Column>() const {
   return date32_columns;
}

template <>
const std::map<std::string, SequenceColumn<Nucleotide>>& Table::getColumns<
   SequenceColumn<Nucleotide>>() const {
   return nuc_columns;
}

template <>
const std::map<std::string, SequenceColumn<AminoAcid>>& Table::getColumns<
   SequenceColumn<AminoAcid>>() const {
   return aa_columns;
}

template <>
const std::map<std::string, ZstdCompressedStringColumn>& Table::getColumns<
   ZstdCompressedStringColumn>() const {
   return zstd_compressed_string_columns;
}

namespace {
class BulkInsertVisitor {
  public:
   template <column::Column ColumnType>
   std::expected<void, std::string> operator()(
      Table& table,
      TableChunkBuilder& block,
      const std::string& name
   ) {
      return table.getColumns<ColumnType>().at(name).appendChunk(
         block.getColumnBuilders<ColumnType>().at(name).finalize()
      );
   }
};
}  // namespace

Table::Table(schema::TableName table_name, std::shared_ptr<schema::TableSchema> schema)
    : table_name(std::move(table_name)),
      schema(std::move(schema)) {
   auto column_initializer = [this]<column::Column ColumnType>(
                                const ColumnIdentifier& column_identifier
                             ) {
      ColumnType column(this->schema->getColumnMetadata<ColumnType>(column_identifier.name).value()
      );
      getColumns<ColumnType>().emplace(column_identifier.name, std::move(column));
   };
   for (const auto& col : this->schema->getColumnIdentifiers()) {
      column::visit(col.type, column_initializer, col);
   }
}

nlohmann::json Table::logTable() const {
   return {
      {"name", table_name.getName()},
      {"primaryKey",
       schema->primary_key.has_value() ? nlohmann::json(schema->primary_key->name)
                                       : nlohmann::json(nullptr)}
   };
}

void Table::validate() const {
   validateNucleotideSequences();
   validateAminoAcidSequences();
   validateMetadataColumns();
}

std::expected<void, std::string> Table::bulkInsert(TableChunkBuilder& block) {
   row_layout.appendChunk(static_cast<uint32_t>(block.numBufferedRows()));
   row_count += block.numBufferedRows();
   for (const auto& column : schema->getColumnIdentifiers()) {
      auto result = column::visit(column.type, BulkInsertVisitor{}, *this, block, column.name);
      if (!result.has_value()) {
         return result;
      }
   }
   return {};
}

void Table::finalize() {
   for (auto& [_, sequence_column] : nuc_columns) {
      sequence_column.finalize();
   }
   for (auto& [_, sequence_column] : aa_columns) {
      sequence_column.finalize();
   }
}

void Table::validatePrimaryKeyUnique() const {
   if (!schema->primary_key.has_value()) {
      SPDLOG_DEBUG("No primary key declared, skipping uniqueness check.");
      return;
   }
   SPDLOG_DEBUG("Checking that primary keys are unique.");
   const auto& primary_key = schema->primary_key.value();
   RHYDB_ASSERT(primary_key.type == schema::ColumnType::STRING);

   const auto& primary_key_column = string_columns.at(primary_key.name);

   std::unordered_set<std::string> unique_keys;
   unique_keys.reserve(row_layout.numRows());
   for (const column::RowId row_id : row_layout) {
      std::string value = primary_key_column.getValueString(row_id);
      if (unique_keys.contains(value)) {
         throw schema::DuplicatePrimaryKeyException("Found duplicate primary key {}", value);
      }
      unique_keys.insert(value);
   }
   SPDLOG_DEBUG("Found {} distinct primary keys.", unique_keys.size());
}

void Table::validateNucleotideSequences() const {
   for (const auto& [name, nuc_column] : nuc_columns) {
      if (nuc_column.sequence_count > row_count) {
         RHYDB_PANIC(
            "nuc_store {} ({}) has invalid size (expected {}).",
            name,
            nuc_column.sequence_count,
            row_count
         );
      }
      if (nuc_column.metadata->reference_sequence.empty()) {
         RHYDB_PANIC("reference_sequence {} is empty.", name);
      }
   }
}

void Table::validateAminoAcidSequences() const {
   for (const auto& [name, aa_column] : aa_columns) {
      if (aa_column.sequence_count > row_count) {
         RHYDB_PANIC(
            "aa_store {} ({}) has invalid size (expected {}).",
            name,
            aa_column.sequence_count,
            row_count
         );
      }
      if (aa_column.metadata->reference_sequence.empty()) {
         RHYDB_PANIC("reference_sequence {} is empty.", name);
      }
   }
}

template <typename Column>
void Table::validateColumnsHaveSize(
   const std::map<std::string, Column>& columnsOfTheType,
   const std::string& columnType
) const {
   for (const auto& col : columnsOfTheType) {
      // Every column is appended to in lockstep with the table's `RowLayout`, so each must hold
      // exactly one chunk per layout chunk. The per-chunk row counts live in the layout itself.
      if (col.second.numChunks() != row_layout.numChunks()) {
         throw preprocessing::PreprocessingException(
            "{} {} has invalid chunk count {} (expected {})",
            columnType,
            col.first,
            col.second.numChunks(),
            row_layout.numChunks()
         );
      }
      const uint16_t num_chunks = row_layout.numChunks();
      for (uint16_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
         if (col.second.chunkSize(chunk_id) != row_layout.chunkSize(chunk_id)) {
            throw preprocessing::PreprocessingException(
               "{} {} has invalid chunk (id={}) size {} (expected {})",
               columnType,
               col.first,
               chunk_id,
               col.second.chunkSize(chunk_id),
               row_layout.chunkSize(chunk_id)
            );
         }
      }
   }
}

void Table::validateMetadataColumns() const {
   validateColumnsHaveSize(date32_columns, "date32_columns");
   validateColumnsHaveSize(bool_columns, "bool_columns");
   validateColumnsHaveSize(int32_columns, "int32_columns");
   validateColumnsHaveSize(int64_columns, "int64_columns");
   validateColumnsHaveSize(dictionary_encoded_columns, "dictionary_encoded_columns");
   validateColumnsHaveSize(string_columns, "string_columns");
   validateColumnsHaveSize(float_columns, "float_columns");
}

namespace {

std::ifstream openInputFileOrThrow(const std::string& path) {
   std::ifstream file(path, std::ios::binary);
   if (!file) {
      auto error = fmt::format("Input file {} could not be opened.", path);
      throw persistence::LoadDatabaseException(error);
   }
   return file;
}

std::ofstream openOutputFileOrThrow(const std::string& path) {
   std::ofstream file(path, std::ios::binary);
   if (!file) {
      auto error = fmt::format("Output file {} could not be opened.", path);
      throw persistence::SaveDatabaseException(error);
   }
   return file;
}

}  // namespace

void Table::saveData(const std::filesystem::path& path) {
   EVOBENCH_SCOPE("Table", "saveData");
   auto output_file = openOutputFileOrThrow(path);
   if (!output_file) {
      throw persistence::SaveDatabaseException(
         "Cannot open output file " + path.string() + " for saving"
      );
   }

   SPDLOG_INFO("Saving table data...");
   ::boost::archive::binary_oarchive output_archive(output_file);
   serializeData(output_archive, 0);
   SPDLOG_INFO("Finished saving table data");
}

void Table::loadData(const std::filesystem::path& path) {
   EVOBENCH_SCOPE("Table", "loadData");

   auto input_file = openInputFileOrThrow(path);
   ::boost::archive::binary_iarchive input_archive(input_file);
   serializeData(input_archive, 0);
   SPDLOG_INFO("Finished loading table data");
}

}  // namespace rhydb::storage
