#include "rhydb/storage/column/row_compaction.h"

#include <utility>
#include <vector>

#include "rhydb/common/bitmap.h"

namespace rhydb::storage::column {

namespace {

/// The position of every row of a chunk of `chunk_size` rows after deleting `deleted_rows`, or
/// nullopt if it is deleted.
std::vector<std::optional<uint16_t>> newRows(
   uint32_t chunk_size,
   roaring_util::RoaringContainerView deleted_rows
) {
   std::vector<std::optional<uint16_t>> new_rows(chunk_size, uint16_t{0});
   for (const uint16_t row_in_chunk : deleted_rows) {
      RHYDB_ASSERT_LT(static_cast<uint32_t>(row_in_chunk), chunk_size);
      new_rows[row_in_chunk] = std::nullopt;
   }
   uint16_t next_row = 0;
   for (auto& new_row : new_rows) {
      if (new_row.has_value()) {
         new_row = next_row++;
      }
   }
   return new_rows;
}

}  // namespace

RowCompaction::RowCompaction(const RowLayout& layout, const Bitmap& deleted_rows)
    : chunks(layout.numChunks()) {
   std::vector<uint32_t> remaining_rows(chunks.size());
   for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      remaining_rows[chunk_id] = layout.chunkSize(static_cast<uint16_t>(chunk_id));
   }
   for (const auto& [chunk_id, rows] : deleted_rows) {
      RHYDB_ASSERT_LT(chunk_id, chunks.size());
      RHYDB_ASSERT_LE(rows.getCardinality(), remaining_rows[chunk_id]);
      remaining_rows[chunk_id] -= rows.getCardinality();
      if (remaining_rows[chunk_id] == 0) {
         continue;
      }
      chunks[chunk_id].new_rows = newRows(layout.chunkSize(chunk_id), rows);
   }
   for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      if (remaining_rows[chunk_id] > 0) {
         chunks[chunk_id].new_chunk_id = static_cast<uint16_t>(compacted_layout.numChunks());
         compacted_layout.appendChunk(remaining_rows[chunk_id]);
      }
   }
}

std::optional<RowId> RowCompaction::map(RowId row_id) const {
   const auto& chunk = chunks.at(row_id.chunk_id);
   if (!chunk.new_chunk_id.has_value()) {
      return std::nullopt;
   }
   if (chunk.new_rows.empty()) {
      return RowId{.chunk_id = *chunk.new_chunk_id, .row_in_chunk = row_id.row_in_chunk};
   }
   return chunk.new_rows.at(row_id.row_in_chunk).transform([&](uint16_t new_row) {
      return RowId{.chunk_id = *chunk.new_chunk_id, .row_in_chunk = new_row};
   });
}

roaring_util::RoaringContainer RowCompaction::compacted(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows
) const {
   const auto& new_rows = chunks.at(chunk_id).new_rows;
   std::vector<uint16_t> remaining_rows;
   for (const uint16_t row : rows) {
      if (const auto new_row = new_rows[row]) {
         remaining_rows.push_back(*new_row);
      }
   }
   // Sized by the remaining rows, so that the result is an array container whenever it is small
   // enough to be one
   auto result =
      roaring_util::RoaringContainer::withCapacity(static_cast<int32_t>(remaining_rows.size()));
   for (const uint16_t row : remaining_rows) {
      result.add(row);
   }
   result.runOptimizeAndShrink();
   return result;
}

void RowCompaction::compact(uint16_t chunk_id, roaring_util::RoaringContainer& rows) const {
   if (!newChunkId(chunk_id).has_value()) {
      rows = roaring_util::RoaringContainer::withCapacity(0);
      return;
   }
   if (!losesSomeRows(chunk_id) || rows.empty()) {
      return;
   }
   rows = compacted(chunk_id, roaring_util::RoaringContainerView{rows});
}

void RowCompaction::compact(std::vector<roaring_util::RoaringContainer>& per_chunk) const {
   RHYDB_ASSERT_EQ(per_chunk.size(), chunks.size());
   for (size_t chunk_id = 0; chunk_id < per_chunk.size(); ++chunk_id) {
      if (losesSomeRows(static_cast<uint16_t>(chunk_id))) {
         compact(static_cast<uint16_t>(chunk_id), per_chunk[chunk_id]);
      }
   }
   dropDeletedChunks(per_chunk);
}

}  // namespace rhydb::storage::column
