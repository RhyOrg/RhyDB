#include "rhydb/storage/column/date32_column.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/date32.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

Date32Column::Date32Column(ColumnMetadata* metadata)
    : metadata(metadata) {}

bool Date32Column::isSorted() const {
   return is_sorted;
}

std::expected<void, std::string> Date32Column::appendChunk(const Buffer& buffer) {
   auto null_rows = roaring_util::RoaringContainer::withCapacity(0);
   std::vector<common::Date32> chunk;
   chunk.reserve(buffer.size());
   for (size_t i = 0; i < buffer.size(); ++i) {
      if (buffer[i].has_value()) {
         if (last_appended_value.has_value() && *buffer[i] < *last_appended_value) {
            is_sorted = false;
         }
         last_appended_value = buffer[i];
         chunk.push_back(*buffer[i]);
      } else {
         null_rows.add(static_cast<uint16_t>(i));
         chunk.push_back(0);
         is_sorted = false;
      }
   }
   values.appendChunk(std::move(chunk));
   null_bitmap.push_back(std::move(null_rows));
   return {};
}

void Date32Column::update(const roaring::Roaring& row_ids, std::optional<common::Date32> value) {
   if (value.has_value()) {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) -= rows;
      }
   } else {
      for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
         null_bitmap.at(chunk_id) |= rows;
      }
   }
   const common::Date32 stored_value = value.value_or(0);
   for (const uint32_t global_row_id : row_ids) {
      values.setValue(RowId::fromGlobal(global_row_id), stored_value);
   }

   // An update can move a value in either direction, so the cheap incremental sortedness tracking
   // of `appendChunk` no longer applies; recompute it by scanning the whole column. As in
   // `appendChunk`, any null makes the column unsorted, and `last_appended_value` is the last
   // non-null value so a subsequent `appendChunk` can keep detecting cross-chunk regressions.
   is_sorted = true;
   last_appended_value = std::nullopt;

   if (!Bitmap::fromChunkContainers(null_bitmap).isEmpty()) {
      is_sorted = false;
      return;
   }
   for (size_t chunk_id = 0; chunk_id < values.numChunks(); ++chunk_id) {
      const std::vector<common::Date32>& chunk = values.chunk(chunk_id);
      for (common::Date32 date : chunk) {
         if (last_appended_value.has_value() && date < *last_appended_value) {
            is_sorted = false;
         }
         last_appended_value = date;
      }
   }
}

namespace {

/// The last value stored before chunk `chunk_id`, skipping empty chunks.
std::optional<common::Date32> lastValueBefore(
   const ChunkedValueBuffer<common::Date32>& values,
   size_t chunk_id
) {
   for (size_t previous = chunk_id; previous-- > 0;) {
      if (!values.chunk(previous).empty()) {
         return values.chunk(previous).back();
      }
   }
   return std::nullopt;
}

/// The first value stored after chunk `chunk_id`, skipping empty chunks.
std::optional<common::Date32> firstValueAfter(
   const ChunkedValueBuffer<common::Date32>& values,
   size_t chunk_id
) {
   for (size_t next = chunk_id + 1; next < values.numChunks(); ++next) {
      if (!values.chunk(next).empty()) {
         return values.chunk(next).front();
      }
   }
   return std::nullopt;
}

}  // namespace

void Date32Column::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));
   bool assigned_null = false;
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const std::optional<common::Date32>& value = new_values[index++];
      if (value.has_value()) {
         null_bitmap.at(chunk_id).removeChecked(row_id.row_in_chunk);
      } else {
         null_bitmap.at(chunk_id).addChecked(row_id.row_in_chunk);
         assigned_null = true;
      }
      values.setValue(row_id, value.value_or(0));
   }

   // A sorted column has no nulls and every other chunk is still sorted, so it stays sorted if the
   // updated chunk is sorted in itself and against its neighbours.
   if (is_sorted) {
      const auto& chunk = values.chunk(chunk_id);
      const auto previous = lastValueBefore(values, chunk_id);
      const auto next = firstValueAfter(values, chunk_id);
      is_sorted = !assigned_null && std::ranges::is_sorted(chunk) &&
                  (chunk.empty() || !previous.has_value() || *previous <= chunk.front()) &&
                  (chunk.empty() || !next.has_value() || chunk.back() <= *next);
      if (is_sorted) {
         last_appended_value = lastValueBefore(values, values.numChunks());
      }
   }
}

}  // namespace rhydb::storage::column
