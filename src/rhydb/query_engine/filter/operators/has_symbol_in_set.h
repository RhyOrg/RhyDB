#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/sequence/sequence_chunk.h"

namespace rhydb::query_engine::filter::operators {

/// Selects the rows of a sequence column whose symbol at a position is one of a set of symbols (a
/// row that does not cover the position has the missing symbol there), and the null rows exactly
/// if `with_null_rows`. Each chunk is evaluated against its own local reference
/// (`storage::sequence::rowsWithSymbolsAt`), so this works whether or not the chunks agree on the
/// reference symbol at the position.
class HasSymbolInSet : public Predicate {
  public:
   /// The chunks of a sequence column, indexed by chunk id
   using Chunks = std::vector<std::shared_ptr<const storage::sequence::SequenceChunk>>;

  private:
   const Chunks* chunks;
   uint32_t position_idx;
   storage::sequence::SymbolSet symbols;
   bool with_null_rows;

  public:
   HasSymbolInSet(
      const Chunks* chunks,
      uint32_t position_idx,
      storage::sequence::SymbolSet symbols,
      bool with_null_rows
   );

   ~HasSymbolInSet() noexcept override;

   [[nodiscard]] std::string toString() const override;
   [[nodiscard]] bool match(storage::column::RowId row_id) const override;
   [[nodiscard]] roaring_util::RoaringContainer makeChunk(
      uint16_t chunk_id,
      uint32_t chunk_size
   ) const override;
   [[nodiscard]] bool evaluatesChunksInBulk() const override { return true; }

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override;
   [[nodiscard]] std::unique_ptr<Predicate> negate() const override;
};

}  // namespace rhydb::query_engine::filter::operators
