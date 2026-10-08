#pragma once

#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

/// Stores a value column's data as the list of fixed-width buffers in which it was ingested, one
/// buffer per appended chunk. Appending never touches previously ingested chunks. A row id is
/// mapped to its value by splitting it into a chunk id and a within-chunk index (see `RowId`), so
/// no offset index is needed: chunk `k` is reached in O(1) and indexed directly.
template <typename T>
class ChunkedValueBuffer {
   std::vector<std::vector<T>> chunks;

  public:
   void appendChunk(std::vector<T>&& values) { chunks.push_back(std::move(values)); }

   [[nodiscard]] size_t numChunks() const { return chunks.size(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const {
      return static_cast<uint32_t>(chunks.at(chunk_id).size());
   }

   /// The raw value buffer of chunk `chunk_idx`. Exposed so a sorted column can be binary searched
   /// in place (see `DateBetween`).
   [[nodiscard]] const std::vector<T>& chunk(size_t chunk_idx) const {
      return chunks.at(chunk_idx);
   }

   [[nodiscard]] const T& at(RowId row_id) const {
      return chunks.at(row_id.chunk_id).at(row_id.row_in_chunk);
   }

   /// Overwrites the value at `row_id` in place. Used by `update` to assign a new scalar value to
   /// an already ingested row; null handling lives in the owning column's bitmaps.
   void setValue(RowId row_id, T value) {
      chunks.at(row_id.chunk_id).at(row_id.row_in_chunk) = std::move(value);
   }

   /// The most recently appended value (the last value of the last chunk).
   [[nodiscard]] const T& lastValue() const { return chunks.back().back(); }

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, const uint32_t /* version */) {
      archive & chunks;
   }
};

}  // namespace rhydb::storage::column
