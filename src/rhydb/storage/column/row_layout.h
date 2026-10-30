#pragma once

#include <algorithm>
#include <iterator>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

/// The shared per-chunk row layout of a table partition. Chunk `k` holds `chunkSize(k)` rows and
/// occupies the 2^16 block of global ids `[k << 16, (k << 16) + chunkSize(k))` (see `RowId`). Every
/// column of the partition is appended in lockstep through `Table::bulkInsert`, so a single
/// `RowLayout` is the source of truth for iterating the partition's rows: columns themselves only
/// offer `RowId`-addressed random access and never need to know how many rows exist.
///
/// Invariant: every chunk holds at least one row. Empty chunks are disallowed so that each chunk
/// maps onto a non-empty 2^16 block and iteration never has to skip over a vacant chunk.
class RowLayout {
   std::vector<uint32_t> chunk_sizes;
   size_t num_rows = 0;

  public:
   template <typename... ChunkSizes>
   static RowLayout of(ChunkSizes... chunk_sizes) {
      RowLayout layout;
      (layout.appendChunk(static_cast<uint32_t>(chunk_sizes)), ...);
      return layout;
   }

   /// Appends a chunk. `chunk_size` must be non-empty, see the class invariant.
   void appendChunk(uint32_t chunk_size) {
      RHYDB_ASSERT_GT(chunk_size, 0U);
      RHYDB_ASSERT_LE(chunk_size, static_cast<uint32_t>(COLUMN_CHUNK_SIZE));
      // TODO(#1329) increase row-limit
      RHYDB_ASSERT_LT(chunk_sizes.size(), static_cast<size_t>(UINT16_MAX));
      chunk_sizes.push_back(chunk_size);
      num_rows += chunk_size;
   }

   [[nodiscard]] size_t numChunks() const { return chunk_sizes.size(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const { return chunk_sizes.at(chunk_id); }

   /// The total number of rows across all chunks. Equals the partition's sequence count.
   [[nodiscard]] uint32_t numRows() const { return num_rows; }

   /// Forward iterator yielding the `RowId` of every row in the partition
   class Iterator {
      const std::vector<uint32_t>* chunk_sizes = nullptr;
      uint16_t chunk_id = 0;
      uint16_t row_in_chunk = 0;

      void skipEmptyChunks() {
         while (chunk_id < chunk_sizes->size() && row_in_chunk >= (*chunk_sizes)[chunk_id]) {
            ++chunk_id;
            row_in_chunk = 0;
         }
      }

     public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = RowId;
      using difference_type = std::ptrdiff_t;
      using pointer = const RowId*;
      using reference = RowId;

      Iterator() = default;

      Iterator(const std::vector<uint32_t>& chunk_sizes, uint16_t chunk_id)
          : chunk_sizes(&chunk_sizes),
            chunk_id(chunk_id) {
         skipEmptyChunks();
      }

      RowId operator*() const { return RowId{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk}; }

      Iterator& operator++() {
         RHYDB_ASSERT_LT(chunk_id, chunk_sizes->size());
         if (row_in_chunk == UINT16_MAX || row_in_chunk == chunk_sizes->at(chunk_id) - 1) {
            ++chunk_id;
            row_in_chunk = 0;
         } else {
            ++row_in_chunk;
         }
         return *this;
      }

      bool operator==(const Iterator& other) const {
         return chunk_id == other.chunk_id && row_in_chunk == other.row_in_chunk;
      }

      bool operator!=(const Iterator& other) const { return !(*this == other); }
   };

   [[nodiscard]] Iterator begin() const { return Iterator{chunk_sizes, 0}; }

   [[nodiscard]] Iterator end() const {
      return Iterator{chunk_sizes, static_cast<uint16_t>(chunk_sizes.size())};
   }

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, const uint32_t /* version */) {
      archive & chunk_sizes;
      // `num_rows` is derived, not serialized; rebuild it from the loaded chunk sizes.
      if constexpr (Archive::is_loading::value) {
         num_rows = 0;
         for (const uint32_t chunk_size : chunk_sizes) {
            num_rows += chunk_size;
         }
      }
   }
};

}  // namespace rhydb::storage::column
