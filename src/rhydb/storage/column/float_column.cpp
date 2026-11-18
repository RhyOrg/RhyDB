#include "rhydb/storage/column/float_column.h"

#include <cstdint>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

FloatColumn::FloatColumn(ColumnMetadata* metadata)
    : metadata(metadata) {}

std::expected<void, std::string> FloatColumn::appendChunk(const Buffer& buffer) {
   auto null_rows = roaring_util::RoaringContainer::withCapacity(0);
   std::vector<double> chunk;
   chunk.reserve(buffer.size());
   for (size_t i = 0; i < buffer.size(); ++i) {
      if (buffer[i].has_value()) {
         chunk.push_back(*buffer[i]);
      } else {
         null_rows.add(static_cast<uint16_t>(i));
         chunk.push_back(0.0);
      }
   }
   values.appendChunk(std::move(chunk));
   null_bitmap.push_back(std::move(null_rows));
   return {};
}

void FloatColumn::update(const roaring::Roaring& row_ids, std::optional<double> value) {
   if (value == std::nullopt) {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) |= rows;
      }
   } else {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) -= rows;
      }
   }
   const double stored_value = value.value_or(0.0);
   for (const uint32_t global_row_id : row_ids) {
      values.setValue(RowId::fromGlobal(global_row_id), stored_value);
   }
}

void FloatColumn::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const std::optional<double>& value = new_values[index++];
      if (value.has_value()) {
         null_bitmap.at(chunk_id).removeChecked(row_id.row_in_chunk);
      } else {
         null_bitmap.at(chunk_id).addChecked(row_id.row_in_chunk);
      }
      values.setValue(row_id, value.value_or(0.0));
   }
}

void FloatColumn::deleteRows(const RowCompaction& compaction) {
   values.deleteRows(compaction);
   compaction.compact(null_bitmap);
}

}  // namespace rhydb::storage::column
