#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::storage::column {

/// How deleting rows renumbers the remaining rows of a table: every chunk keeps its remaining rows
/// in their order, packed to the front of the chunk, and a chunk without remaining rows is dropped,
/// so the chunks after it move down. The result is again a dense `RowLayout` whose chunks are all
/// non-empty. Every structure keyed by row ids compacts itself with this, so that the rows keep
/// lining up across the columns of the table and with its `RowLayout`.
///
/// The structures are compacted in place: the data of chunks without deleted rows is only
/// relabelled with the chunk's new id, and only the chunks that lose rows are touched.
class RowCompaction {
   struct ChunkCompaction {
      /// The id of the chunk after the compaction, or nullopt if all its rows are deleted
      std::optional<uint16_t> new_chunk_id;
      /// The rows of the chunk that are deleted (as rows within the chunk); empty if none is
      roaring_util::RoaringContainer deleted_rows = roaring_util::RoaringContainer::withCapacity(0);
   };

   std::vector<ChunkCompaction> chunks;
   RowLayout compacted_layout;

   /// The remaining ones of `rows`, rows of chunk `chunk_id`, renumbered within their chunk. May be
   /// empty.
   [[nodiscard]] roaring_util::RoaringContainer compacted(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows
   ) const;

  public:
   /// Compacts `layout` by deleting `deleted_rows`, which must be valid row ids of `layout`. The
   /// deleted rows are copied, so `deleted_rows` may view into the structures being compacted.
   RowCompaction(const RowLayout& layout, const Bitmap& deleted_rows);

   /// The layout of the rows that remain.
   [[nodiscard]] const RowLayout& compactedLayout() const { return compacted_layout; }

   /// The id of the chunk `chunk_id` after the compaction, or nullopt if all its rows are deleted.
   [[nodiscard]] std::optional<uint16_t> newChunkId(uint16_t chunk_id) const {
      return chunks.at(chunk_id).new_chunk_id;
   }

   /// Whether chunk `chunk_id` keeps some, but not all of its rows.
   [[nodiscard]] bool losesSomeRows(uint16_t chunk_id) const {
      const auto& chunk = chunks.at(chunk_id);
      return chunk.new_chunk_id.has_value() && !chunk.deleted_rows.empty();
   }

   /// The id of the row `row_id` after the compaction, or nullopt if it is deleted.
   [[nodiscard]] std::optional<RowId> map(RowId row_id) const;

   /// Removes the deleted rows from `rows`, the rows of chunk `chunk_id`, and renumbers the
   /// remaining ones within the chunk they move to (see `newChunkId`). `rows` may become empty.
   void compact(uint16_t chunk_id, roaring_util::RoaringContainer& rows) const;

   /// Compacts per-chunk row sets, where `per_chunk[k]` holds the rows of chunk `k`: removes the
   /// deleted rows, renumbers the remaining ones and drops the chunks without remaining rows.
   void compact(std::vector<roaring_util::RoaringContainer>& per_chunk) const;

   /// Packs the remaining rows of chunk `chunk_id` to the front of the chunk, which holds
   /// `chunk_size` rows: calls `move_row(from, to)` for every remaining row whose position in the
   /// chunk changes, in ascending order, so that `to < from` and `to` is free. Returns the number
   /// of remaining rows.
   template <typename MoveRow>
   uint32_t compactRows(uint16_t chunk_id, uint32_t chunk_size, const MoveRow& move_row) const {
      const roaring_util::RoaringContainerView deleted_rows{chunks.at(chunk_id).deleted_rows};
      uint32_t deleted_before = 0;
      uint32_t next_row = 0;
      auto move_rows_up_to = [&](uint32_t end) {
         if (deleted_before > 0) {
            for (uint32_t row = next_row; row < end; ++row) {
               move_row(row, row - deleted_before);
            }
         }
      };
      for (const uint32_t deleted_row : deleted_rows) {
         move_rows_up_to(deleted_row);
         ++deleted_before;
         next_row = deleted_row + 1;
      }
      move_rows_up_to(chunk_size);
      return chunk_size - deleted_before;
   }

   /// Removes the elements of the chunks that lose all their rows from `per_chunk`, where
   /// `per_chunk[k]` belongs to chunk `k`, moving the others down. Works with any sequence
   /// container that can erase, e.g. a `std::vector` or `std::deque`.
   template <typename PerChunk>
   void dropDeletedChunks(PerChunk& per_chunk) const {
      RHYDB_ASSERT_EQ(per_chunk.size(), chunks.size());
      size_t remaining_chunks = 0;
      for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
         if (!chunks[chunk_id].new_chunk_id.has_value()) {
            continue;
         }
         if (remaining_chunks != chunk_id) {
            per_chunk[remaining_chunks] = std::move(per_chunk[chunk_id]);
         }
         ++remaining_chunks;
      }
      per_chunk.erase(per_chunk.begin() + remaining_chunks, per_chunk.end());
   }

   /// Compacts per-chunk row data in place, where `per_chunk[k][i]` belongs to the row `i` of chunk
   /// `k`: removes the elements of the deleted rows and the chunks without remaining rows.
   template <typename T>
   void compactChunks(std::vector<std::vector<T>>& per_chunk) const {
      RHYDB_ASSERT_EQ(per_chunk.size(), chunks.size());
      for (size_t chunk_id = 0; chunk_id < per_chunk.size(); ++chunk_id) {
         if (!losesSomeRows(static_cast<uint16_t>(chunk_id))) {
            continue;
         }
         auto& values = per_chunk[chunk_id];
         const uint32_t remaining_rows = compactRows(
            static_cast<uint16_t>(chunk_id),
            static_cast<uint32_t>(values.size()),
            [&](uint32_t from, uint32_t to) { values[to] = std::move(values[from]); }
         );
         values.erase(values.begin() + remaining_rows, values.end());
      }
      dropDeletedChunks(per_chunk);
   }
};

}  // namespace rhydb::storage::column
