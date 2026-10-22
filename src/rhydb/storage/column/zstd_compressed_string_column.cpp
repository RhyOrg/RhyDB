#include "rhydb/storage/column/zstd_compressed_string_column.h"

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

ZstdCompressedStringColumnMetadata::ZstdCompressedStringColumnMetadata(
   std::string column_name,
   std::string dictionary_string
)
    : ColumnMetadata(std::move(column_name)),
      compressor(std::make_shared<ZstdCDictionary>(dictionary_string, 3)),
      decompressor(std::make_shared<ZstdDDictionary>(dictionary_string)),
      dictionary_string(std::move(dictionary_string)) {}

ZstdCompressedStringColumn::ZstdCompressedStringColumn(
   rhydb::storage::column::ZstdCompressedStringColumn::Metadata* metadata
)
    : metadata(metadata) {}

std::expected<void, std::string> ZstdCompressedStringColumn::appendChunk(const Buffer& buffer) {
   auto null_rows = roaring_util::RoaringContainer::withCapacity(0);
   std::vector<std::string> chunk;
   chunk.reserve(buffer.size());
   for (size_t i = 0; i < buffer.size(); ++i) {
      const auto& value = buffer[i];
      if (value.has_value()) {
         chunk.emplace_back(*value);
      } else {
         null_rows.add(static_cast<uint16_t>(i));
         chunk.emplace_back();
      }
   }
   values.appendChunk(std::move(chunk));
   null_bitmap.push_back(std::move(null_rows));
   return {};
}

void ZstdCompressedStringColumn::update(
   const roaring::Roaring& row_ids,
   const std::optional<std::string>& value
) {
   // An empty stored buffer denotes null (see `appendChunk`/`getDecompressed`); a concrete value is
   // compressed once and the same bytes are written to every matched row.
   std::string stored_value;
   if (value.has_value()) {
      stored_value = metadata->compressor.compress(value->data(), value->size());
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) -= rows;
      }
   } else {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) |= rows;
      }
   }
   for (const uint32_t global_row_id : row_ids) {
      values.setValue(RowId::fromGlobal(global_row_id), stored_value);
   }
}

void ZstdCompressedStringColumn::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));
   // An empty stored buffer denotes null (see `appendChunk`/`getDecompressed`).
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const std::optional<std::string>& value = new_values[index++];
      if (value.has_value()) {
         null_bitmap.at(chunk_id).removeChecked(row_id.row_in_chunk);
      } else {
         null_bitmap.at(chunk_id).addChecked(row_id.row_in_chunk);
      }
      values.setValue(row_id, value.value_or(std::string{}));
   }
}

void ZstdCompressedStringColumn::deleteRows(const RowCompaction& compaction) {
   values.deleteRows(compaction);
   compaction.compact(null_bitmap);
}

bool ZstdCompressedStringColumn::isNull(RowId row_id) const {
   return null_bitmap[row_id.chunk_id].contains(row_id.row_in_chunk);
}

std::optional<std::string> ZstdCompressedStringColumn::getDecompressed(RowId row_id) const {
   const auto value = values.at(row_id);
   if (value.empty()) {
      return std::nullopt;
   }
   std::string result_buffer;
   metadata->decompressor.decompress(value, result_buffer);
   return result_buffer;
}

std::optional<std::string> ZstdCompressedStringColumn::getCompressed(RowId row_id) const {
   auto value = values.at(row_id);
   if (value.empty()) {
      return std::nullopt;
   }
   return value;
}

}  // namespace rhydb::storage::column
