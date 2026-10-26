#include "rhydb/storage/column/bool_column.h"

#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

BoolColumn::BoolColumn(ColumnMetadata* metadata)
    : metadata(metadata) {}

std::expected<void, std::string> BoolColumn::appendChunk(const Buffer& buffer) {
   const uint32_t base = RowId::chunkStart(num_chunks);
   auto null_rows = roaring_util::RoaringContainer::withCapacity(0);
   for (size_t i = 0; i < buffer.size(); ++i) {
      const uint32_t row_id = base + static_cast<uint32_t>(i);
      const auto& value = buffer[i];
      if (!value.has_value()) {
         null_rows.add(static_cast<uint16_t>(i));
      } else if (*value) {
         true_bitmap.add(row_id);
      } else {
         false_bitmap.add(row_id);
      }
   }
   null_bitmap.push_back(std::move(null_rows));
   num_chunks++;
   return {};
}

void BoolColumn::update(const roaring::Roaring& row_ids, std::optional<bool> value) {
   if (value == std::nullopt) {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) |= rows;
      }
   } else {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) -= rows;
      }
   }
   if (value == true) {
      true_bitmap |= row_ids;
   } else {
      true_bitmap -= row_ids;
   }
   if (value == false) {
      false_bitmap |= row_ids;
   } else {
      false_bitmap -= row_ids;
   }
}

void BoolColumn::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const uint32_t global_row_id = row_id.toGlobal();
      const std::optional<bool>& value = new_values[index++];
      // Re-classify the row into exactly one of the three bitmaps
      null_bitmap.at(chunk_id).removeChecked(row_id.row_in_chunk);
      true_bitmap.remove(global_row_id);
      false_bitmap.remove(global_row_id);
      if (!value.has_value()) {
         null_bitmap.at(chunk_id).addChecked(row_id.row_in_chunk);
      } else if (*value) {
         true_bitmap.add(global_row_id);
      } else {
         false_bitmap.add(global_row_id);
      }
   }
}
}  // namespace rhydb::storage::column
