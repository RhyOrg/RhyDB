#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <simdjson.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/bool_column.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/float_column.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

namespace rhydb::storage {

class Table;

/// Accumulates one ingestion chunk (at most column::COLUMN_CHUNK_SIZE rows) by
/// buffering the extracted value of each row into a per-column builder. The
/// finalized chunks are handed to Table::bulkInsert, which applies them to the
/// columns' global structures.
class TableChunkBuilder {
  public:
   std::vector<schema::ColumnIdentifier> metadata;

   std::map<std::string, column::StringColumn::Builder> string_column_builders;
   std::map<std::string, column::DictionaryEncodedColumn::Builder>
      dictionary_encoded_column_builders;
   std::map<std::string, column::BoolColumn::Builder> bool_column_builders;
   std::map<std::string, column::Int32Column::Builder> int32_column_builders;
   std::map<std::string, column::Int64Column::Builder> int64_column_builders;
   std::map<std::string, column::FloatColumn::Builder> float_column_builders;
   std::map<std::string, column::Date32Column::Builder> date32_column_builders;
   std::map<std::string, column::SequenceColumn<Nucleotide>::Builder> nuc_column_builders;
   std::map<std::string, column::SequenceColumn<AminoAcid>::Builder> aa_column_builders;
   std::map<std::string, column::ZstdCompressedStringColumn::Builder>
      zstd_compressed_string_column_builders;

   /// The sequence column builders are seeded with each column's current
   /// (possibly adapted) local reference, so sequences buffered into a chunk are
   /// diffed against the same reference basis as the already-stored rows.
   explicit TableChunkBuilder(const Table& table);

   /// Extract the value of one column for the current row from the json line and
   /// buffer it into the matching column builder.
   std::expected<void, std::string> addJsonValueToColumn(
      const schema::ColumnIdentifier& column_identifier,
      simdjson::ondemand::value& value
   );

   /// Move buffered row `index` (all columns) from this builder into `destination`, appending it
   /// there. Used by the repartitioning step to distribute a full input buffer across the
   /// per-cluster output buffers. The moved-from slots remain in this builder; call clear() after.
   void moveRowTo(size_t index, TableChunkBuilder& destination);

   /// Drop all buffered rows (resets the input buffer once its rows have been moved out).
   void clear();

   /// The covered [start, end) of buffered row `index` for the given sequence column, or nullopt
   /// for a null / fully-missing row. Lets a row's output partition be chosen without consuming the
   /// buffer. `sequence_column` must be a nucleotide or amino-acid sequence column.
   [[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> coverageRangeAt(
      const schema::ColumnIdentifier& sequence_column,
      size_t index
   ) const;

   /// Number of rows buffered into the current chunk.
   [[nodiscard]] size_t numBufferedRows() const;

   template <column::Column ColumnType>
   std::map<std::string, typename ColumnType::Builder>& getColumnBuilders();
};

template <>
std::map<std::string, column::StringColumn::Builder>& TableChunkBuilder::getColumnBuilders<
   column::StringColumn>();
template <>
std::map<std::string, column::DictionaryEncodedColumn::Builder>& TableChunkBuilder::
   getColumnBuilders<column::DictionaryEncodedColumn>();
template <>
std::map<std::string, column::BoolColumn::Builder>& TableChunkBuilder::getColumnBuilders<
   column::BoolColumn>();
template <>
std::map<std::string, column::Int32Column::Builder>& TableChunkBuilder::getColumnBuilders<
   column::Int32Column>();
template <>
std::map<std::string, column::Int64Column::Builder>& TableChunkBuilder::getColumnBuilders<
   column::Int64Column>();
template <>
std::map<std::string, column::FloatColumn::Builder>& TableChunkBuilder::getColumnBuilders<
   column::FloatColumn>();
template <>
std::map<std::string, column::Date32Column::Builder>& TableChunkBuilder::getColumnBuilders<
   column::Date32Column>();
template <>
std::map<std::string, column::SequenceColumn<Nucleotide>::Builder>& TableChunkBuilder::
   getColumnBuilders<column::SequenceColumn<Nucleotide>>();
template <>
std::map<std::string, column::SequenceColumn<AminoAcid>::Builder>& TableChunkBuilder::
   getColumnBuilders<column::SequenceColumn<AminoAcid>>();
template <>
std::map<std::string, column::ZstdCompressedStringColumn::Builder>& TableChunkBuilder::
   getColumnBuilders<column::ZstdCompressedStringColumn>();

}  // namespace rhydb::storage
