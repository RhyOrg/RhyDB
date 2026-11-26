#include "rhydb/storage/column/binary_column.h"

#include <utility>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"

namespace rhydb::storage::column {

BinaryColumn::BinaryColumn(Metadata* metadata)
    : metadata(metadata) {}

std::expected<void, std::string> BinaryColumn::appendChunk(const Buffer& buffer) {
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

void BinaryColumn::update(
   const roaring::Roaring& row_ids,
   const std::optional<std::string>& value
) {
   for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
      if (value.has_value()) {
         null_bitmap.at(chunk_id) -= rows;
      } else {
         null_bitmap.at(chunk_id) |= rows;
      }
   }
   const std::string stored_value = value.value_or(std::string{});
   for (const uint32_t global_row_id : row_ids) {
      values.setValue(RowId::fromGlobal(global_row_id), stored_value);
   }
}

void BinaryColumn::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const std::optional<std::string>& value = new_values[index++];
      if (value.has_value()) {
         null_bitmap.at(chunk_id).removeChecked(row_in_chunk);
      } else {
         null_bitmap.at(chunk_id).addChecked(row_in_chunk);
      }
      values.setValue(row_id, value.value_or(std::string{}));
   }
}

void BinaryColumn::deleteRows(const RowCompaction& compaction) {
   values.deleteRows(compaction);
   compaction.compact(null_bitmap);
}

bool BinaryColumn::isNull(RowId row_id) const {
   return null_bitmap[row_id.chunk_id].contains(row_id.row_in_chunk);
}

}  // namespace rhydb::storage::column
