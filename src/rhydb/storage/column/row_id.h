#pragma once

#include <cstdint>

#include "rhydb/storage/column/column.h"

namespace rhydb::storage::column {

/// Decomposition of a 32-bit global row id into the chunk that owns the row (the
/// high 16 bits) and the row's index within that chunk (the low 16 bits). Each
/// appended chunk occupies exactly one 2^16 block of the row-id space, so chunk
/// `k` spans the global ids `[k << 16, (k << 16) + chunk_size)` and holds at most
/// `COLUMN_CHUNK_SIZE == 1 << 16` rows. This is the same high/low split that
/// Roaring bitmaps use for their containers, so a chunk maps one-to-one onto a
/// Roaring container block.
struct RowId {
   uint16_t chunk_id;
   uint16_t row_in_chunk;

   bool operator==(const RowId& start) const = default;

   static constexpr RowId fromGlobal(uint32_t global_row_id) {
      return {
         .chunk_id = static_cast<uint16_t>(global_row_id >> 16),
         .row_in_chunk = static_cast<uint16_t>(global_row_id & 0xFFFF),
      };
   }

   [[nodiscard]] constexpr uint32_t toGlobal() const {
      return (static_cast<uint32_t>(chunk_id) << 16) | row_in_chunk;
   }

   /// The global id at which chunk `chunk_id` begins.
   static constexpr uint32_t chunkStart(uint16_t chunk_id) {
      return static_cast<uint32_t>(chunk_id) << 16;
   }
};

static_assert(COLUMN_CHUNK_SIZE == (1UL << 16), "RowId assumes 2^16-sized chunks");

}  // namespace rhydb::storage::column
