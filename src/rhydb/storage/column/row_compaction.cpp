#include "rhydb/storage/column/row_compaction.h"

#include <utility>
#include <vector>

#include "rhydb/common/bitmap.h"

namespace rhydb::storage::column {

namespace {

/// Renumbers the rows of a chunk, which must be passed in ascending order: a remaining row moves
/// down by the number of deleted rows before it.
class RowRenumbering {
   roaring_util::RoaringContainerView::ConstIterator next_deleted;
   roaring_util::RoaringContainerView::ConstIterator deleted_end;
   uint16_t deleted_before = 0;

  public:
   /// `deleted_rows` must outlive this object.
   explicit RowRenumbering(const roaring_util::RoaringContainer& deleted_rows)
       : next_deleted(roaring_util::RoaringContainerView{deleted_rows}.begin()),
         deleted_end(roaring_util::RoaringContainerView::end()) {}

   /// The new position of `row` within its chunk, or nullopt if it is deleted.
   std::optional<uint16_t> operator()(uint16_t row) {
      while (next_deleted != deleted_end && *next_deleted < row) {
         ++next_deleted;
         ++deleted_before;
      }
      if (next_deleted != deleted_end && *next_deleted == row) {
         return std::nullopt;
      }
      return static_cast<uint16_t>(row - deleted_before);
   }
};

}  // namespace

RowCompaction::RowCompaction(const RowLayout& layout, const Bitmap& deleted_rows)
    : chunks(layout.numChunks()) {
   for (const auto& [chunk_id, rows] : deleted_rows) {
      RHYDB_ASSERT_LT(chunk_id, chunks.size());
      for (const uint16_t row_in_chunk : rows) {
         RHYDB_ASSERT_LT(static_cast<uint32_t>(row_in_chunk), layout.chunkSize(chunk_id));
      }
      chunks[chunk_id].deleted_rows = rows.toOwning();
   }
   for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      auto& chunk = chunks[chunk_id];
      const auto remaining_rows =
         (layout.chunkSize(static_cast<uint16_t>(chunk_id)) - chunk.deleted_rows.getCardinality());
      if (remaining_rows > 0) {
         chunk.new_chunk_id = static_cast<uint16_t>(compacted_layout.numChunks());
         compacted_layout.appendChunk(remaining_rows);
      }
   }
}

std::optional<RowId> RowCompaction::map(RowId row_id) const {
   const auto& chunk = chunks.at(row_id.chunk_id);
   if (!chunk.new_chunk_id.has_value() || chunk.deleted_rows.contains(row_id.row_in_chunk)) {
      return std::nullopt;
   }
   // The row itself is not deleted, so the rank counts the deleted rows before it
   const auto deleted_before = static_cast<uint16_t>(chunk.deleted_rows.rank(row_id.row_in_chunk));
   return RowId{
      .chunk_id = *chunk.new_chunk_id,
      .row_in_chunk = static_cast<uint16_t>(row_id.row_in_chunk - deleted_before),
   };
}

roaring_util::RoaringContainer RowCompaction::compacted(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows
) const {
   RowRenumbering renumber{chunks.at(chunk_id).deleted_rows};
   std::vector<uint16_t> remaining_rows;
   for (const uint16_t row : rows) {
      if (const auto new_row = renumber(row)) {
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
