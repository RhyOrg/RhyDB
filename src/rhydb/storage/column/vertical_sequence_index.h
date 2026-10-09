#pragma once

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/map.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/symbol_map.h"
#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::storage::column {

// For documentation of this data-structure see:
// documentation/developer/sequence_storage.md
template <typename SymbolType>
class VerticalSequenceIndex {
  public:
   struct SequenceDiffKey {
      // The position in the bitmap
      uint32_t position;
      uint16_t v_index;
      SymbolType::Symbol symbol;

      auto operator<=>(const SequenceDiffKey&) const = default;
      bool operator==(const SequenceDiffKey&) const = default;

      template <class Archive>
      void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
         // clang-format off
         archive & position;
         archive & v_index;
         archive & symbol;
         // clang-format on
      }
   };
   static_assert(sizeof(SequenceDiffKey) == 8);

   using SequenceDiff = roaring_util::RoaringContainer;

   std::map<SequenceDiffKey, SequenceDiff> vertical_bitmaps;

   using const_iterator = std::map<SequenceDiffKey, SequenceDiff>::const_iterator;

   void addSymbolsToPositions(
      uint32_t position_idx,
      const SymbolMap<SymbolType, std::vector<uint32_t>>& ids_per_symbol
   );

   [[nodiscard]] std::pair<const_iterator, const_iterator> getRangeForPosition(
      uint32_t position_idx
   ) const;

   [[nodiscard]] SymbolMap<SymbolType, uint32_t> computeSymbolCountsForPosition(
      std::map<SequenceDiffKey, SequenceDiff>::const_iterator start,
      std::map<SequenceDiffKey, SequenceDiff>::const_iterator end,
      SymbolType::Symbol current_local_reference_symbol,
      uint32_t coverage_cardinality
   ) const;

   [[nodiscard]] SymbolType::Symbol getSymbolWithHighestCount(
      const SymbolMap<SymbolType, uint32_t>& symbol_counts,
      SymbolType::Symbol current_local_reference_symbol
   ) const;

   /// The symbol that should replace the current local reference symbol at this position, or
   /// nullopt if the current one is already the most common. Only needs the number of rows
   /// covering the position, not the (expensive to materialize) bitmap of those rows, so it can
   /// cheaply decide whether adaptLocalReference needs to run at all.
   [[nodiscard]] std::optional<typename SymbolType::Symbol> findBetterLocalReferenceSymbol(
      uint32_t position_idx,
      SymbolType::Symbol current_local_reference_symbol,
      uint64_t coverage_cardinality
   ) const;

   std::optional<typename SymbolType::Symbol> adaptLocalReference(
      const roaring::Roaring& coverage_bitmap,
      uint32_t position_idx,
      SymbolType::Symbol current_local_reference_symbol
   );

   SequenceDiff& getContainerOrCreateWithCapacity(const SequenceDiffKey& key, int32_t capacity);

   [[nodiscard]] roaring::Roaring getMatchingContainersAsBitmap(
      uint32_t position_idx,
      std::vector<typename SymbolType::Symbol> symbol
   ) const;

   /// Non-owning views of the stored containers at `position_idx` whose symbol is in `symbols`,
   /// paired with their `v_index` (the high 16 bits of the row id). Unlike
   /// `getMatchingContainersAsBitmap` this clones nothing: the caller can assemble the row set
   /// container-by-container as views into this index, which must outlive them. A `v_index` may
   /// appear more than once when several requested symbols occur in the same 2^16 block.
   [[nodiscard]] std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>>
   getMatchingContainerViews(
      uint32_t position_idx,
      const std::vector<typename SymbolType::Symbol>& symbols
   ) const;

   void overwriteSymbolsInSequences(
      std::vector<std::string>& sequences,
      const Bitmap& row_ids
   ) const;

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
      archive & vertical_bitmaps;
   }
};

std::vector<std::pair<uint16_t, std::vector<uint16_t>>> splitIdsIntoBatches(
   const std::vector<uint32_t>& sorted_ids
);

}  // namespace rhydb::storage::column
