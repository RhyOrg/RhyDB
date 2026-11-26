#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/sequence/sequence_chunk.h"

namespace rhydb::query_engine::filter::operators {

class IsInCoveredRegion : public Predicate {
  public:
   enum class Comparator : uint8_t { IS_COVERED, IS_NOT_COVERED };

   /// The chunks of a sequence column, indexed by chunk id
   using Chunks = std::vector<std::shared_ptr<const storage::sequence::SequenceChunk>>;

  private:
   const Chunks* chunks;
   uint32_t position_idx;
   Comparator comparator;

  public:
   explicit IsInCoveredRegion(const Chunks* chunks, uint32_t position_idx, Comparator comparator);

   ~IsInCoveredRegion() noexcept override;

   [[nodiscard]] std::string toString() const override;
   [[nodiscard]] bool isCovered(storage::column::RowId row_id) const;
   [[nodiscard]] bool match(storage::column::RowId row_id) const override;
   [[nodiscard]] roaring_util::RoaringContainer makeChunk(
      uint16_t chunk_id,
      uint32_t chunk_size
   ) const override;
   [[nodiscard]] bool evaluatesChunksInBulk() const override { return true; }
   [[nodiscard]] double estimateSelectivity(uint32_t row_count) const override;

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override;
   [[nodiscard]] std::unique_ptr<Predicate> negate() const override;
};

}  // namespace rhydb::query_engine::filter::operators
