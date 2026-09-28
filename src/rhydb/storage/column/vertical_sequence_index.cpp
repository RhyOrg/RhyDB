#include "rhydb/storage/column/vertical_sequence_index.h"

#include <cstdint>
#include <optional>
#include <roaring/roaring.hh>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/symbol_map.h"
#include "rhydb/roaring_util/bitmap_builder.h"
#include "rhydb/roaring_util/subset_ranks.h"

namespace rhydb::storage::column {

template <typename SymbolType>
void VerticalSequenceIndex<SymbolType>::addSymbolsToPositions(
   uint32_t position_idx,
   const SymbolMap<SymbolType, std::vector<uint32_t>>& ids_per_symbol
) {
   for (const auto& symbol : SymbolType::SYMBOLS) {
      const std::vector<std::pair<uint16_t, std::vector<uint16_t>>> ids_in_batches =
         splitIdsIntoBatches(ids_per_symbol.at(symbol));
      for (const auto& [upper_bits, lower_bits_vector] : ids_in_batches) {
         RHYDB_ASSERT_GT(lower_bits_vector.size(), 0ULL);

         const SequenceDiffKey key{
            .position = position_idx, .v_index = upper_bits, .symbol = symbol,
         };
         SequenceDiff& sequence_diff =
            getContainerOrCreateWithCapacity(key, lower_bits_vector.size());

         for (auto id_lower_bits : lower_bits_vector) {
            sequence_diff.add(id_lower_bits);
         }
      }
   }
}

template <typename SymbolType>
using const_iterator = typename VerticalSequenceIndex<SymbolType>::const_iterator;

template <typename SymbolType>
std::pair<const_iterator<SymbolType>, const_iterator<SymbolType>> VerticalSequenceIndex<
   SymbolType>::getRangeForPosition(uint32_t position_idx) const {
   return {
      vertical_bitmaps.lower_bound(
         SequenceDiffKey{position_idx, 0, static_cast<SymbolType::Symbol>(0)}
      ),
      vertical_bitmaps.lower_bound(
         SequenceDiffKey{position_idx + 1, 0, static_cast<SymbolType::Symbol>(0)}
      ),
   };
}

template <typename SymbolType>
SymbolMap<SymbolType, uint32_t> VerticalSequenceIndex<SymbolType>::computeSymbolCountsForPosition(
   std::map<SequenceDiffKey, SequenceDiff>::const_iterator start,
   std::map<SequenceDiffKey, SequenceDiff>::const_iterator end,
   SymbolType::Symbol current_local_reference_symbol,
   uint32_t coverage_cardinality
) const {
   SymbolMap<SymbolType, uint32_t> symbol_counts;
   for (const auto& symbol : SymbolType::SYMBOLS) {
      symbol_counts[symbol] = 0;
   }
   symbol_counts[current_local_reference_symbol] = coverage_cardinality;

   for (auto it = start; it != end; ++it) {
      const auto& [sequence_diff_key, sequence_diff] = *it;
      RHYDB_ASSERT(sequence_diff_key.symbol != current_local_reference_symbol);
      symbol_counts[sequence_diff_key.symbol] += sequence_diff.getCardinality();
      symbol_counts[current_local_reference_symbol] -= sequence_diff.getCardinality();
   }
   return symbol_counts;
}

template <typename SymbolType>
SymbolType::Symbol VerticalSequenceIndex<SymbolType>::getSymbolWithHighestCount(
   const SymbolMap<SymbolType, uint32_t>& symbol_counts,
   SymbolType::Symbol current_local_reference_symbol
) const {
   // Find the symbol with the highest count
   typename SymbolType::Symbol best_symbol = current_local_reference_symbol;
   uint32_t best_count = symbol_counts.at(current_local_reference_symbol);
   for (const auto& symbol : SymbolType::SYMBOLS) {
      if (symbol == current_local_reference_symbol) {
         continue;
      }
      if (symbol_counts.at(symbol) > best_count) {
         best_symbol = symbol;
         best_count = symbol_counts.at(symbol);
      }
   }
   return best_symbol;
}

template <typename SymbolType>
std::optional<typename SymbolType::Symbol> VerticalSequenceIndex<SymbolType>::
   findBetterLocalReferenceSymbol(
      uint32_t position_idx,
      SymbolType::Symbol current_local_reference_symbol,
      uint64_t coverage_cardinality
   ) const {
   auto [start, end] = getRangeForPosition(position_idx);

   const SymbolMap<SymbolType, uint32_t> symbol_counts = computeSymbolCountsForPosition(
      start, end, current_local_reference_symbol, coverage_cardinality
   );
   const typename SymbolType::Symbol best_symbol =
      getSymbolWithHighestCount(symbol_counts, current_local_reference_symbol);
   if (best_symbol == current_local_reference_symbol) {
      return std::nullopt;
   }
   return best_symbol;
}

template <typename SymbolType>
std::optional<typename SymbolType::Symbol> VerticalSequenceIndex<SymbolType>::adaptLocalReference(
   const roaring::Roaring& coverage_bitmap,
   uint32_t position_idx,
   SymbolType::Symbol current_local_reference_symbol
) {
   auto [start, end] = getRangeForPosition(position_idx);

   const auto best_symbol = findBetterLocalReferenceSymbol(
      position_idx, current_local_reference_symbol, coverage_bitmap.cardinality()
   );
   if (!best_symbol.has_value()) {
      return std::nullopt;
   }
   const typename SymbolType::Symbol new_reference_symbol = best_symbol.value();
   roaring::Roaring old_reference_bitmap = coverage_bitmap;

   old_reference_bitmap -= getMatchingContainersAsBitmap(
      position_idx, {SymbolType::SYMBOLS.begin(), SymbolType::SYMBOLS.end()}
   );

   const auto& roaring_array = old_reference_bitmap.roaring.high_low_container;
   RHYDB_ASSERT_LT(roaring_array.size, UINT16_MAX);
   auto num_containers = static_cast<uint16_t>(roaring_array.size);
   for (uint16_t container_idx = 0; container_idx < num_containers; ++container_idx) {
      const uint8_t typecode = roaring_array.typecodes[container_idx];
      const uint16_t v_index = roaring_array.keys[container_idx];

      auto key = SequenceDiffKey{position_idx, v_index, current_local_reference_symbol};
      vertical_bitmaps.insert(
         {key, SequenceDiff::clonedFrom(roaring_array.containers[container_idx], typecode)}
      );
   }

   std::vector<uint16_t> v_indices_to_remove;
   for (auto it = start; it != end; ++it) {
      const auto& [sequence_diff_key, sequence_diff] = *it;
      if (sequence_diff_key.symbol == new_reference_symbol) {
         v_indices_to_remove.push_back(sequence_diff_key.v_index);
      }
   }
   for (auto v_index : v_indices_to_remove) {
      vertical_bitmaps.erase(SequenceDiffKey{position_idx, v_index, new_reference_symbol});
   }

   return new_reference_symbol;
}

template <typename SymbolType>
VerticalSequenceIndex<SymbolType>::SequenceDiff& VerticalSequenceIndex<
   SymbolType>::getContainerOrCreateWithCapacity(const SequenceDiffKey& key, int32_t capacity) {
   auto iter = vertical_bitmaps.find(key);
   if (iter != vertical_bitmaps.end()) {
      return iter->second;
   }
   return vertical_bitmaps.insert({key, SequenceDiff::withCapacity(capacity)}).first->second;
}

using rhydb::roaring_util::BitmapBuilderByContainer;

template <typename SymbolType>
roaring::Roaring VerticalSequenceIndex<SymbolType>::getMatchingContainersAsBitmap(
   uint32_t position_idx,
   std::vector<typename SymbolType::Symbol> symbols
) const {
   // We compute the range of bitmap containers that are relevant for our position
   auto [start, end] = getRangeForPosition(position_idx);

   // We need to union all bitmap containers at this position

   BitmapBuilderByContainer builder;

   for (auto it = start; it != end; ++it) {
      const auto& [sequence_diff_key, sequence_diff] = *it;
      RHYDB_ASSERT(!sequence_diff.empty());

      // Only consider when the symbol is in the requested set
      if (std::find(symbols.begin(), symbols.end(), sequence_diff_key.symbol) == symbols.end()) {
         continue;
      }
      builder.addContainer(
         sequence_diff_key.v_index, sequence_diff.rawContainer(), sequence_diff.getTypecode()
      );
   }
   return std::move(builder).getBitmap();
}

template <typename SymbolType>
std::vector<std::pair<uint16_t, rhydb::roaring_util::RoaringContainerView>> VerticalSequenceIndex<
   SymbolType>::
   getMatchingContainerViews(
      uint32_t position_idx,
      const std::vector<typename SymbolType::Symbol>& symbols
   ) const {
   auto [start, end] = getRangeForPosition(position_idx);

   std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>> result;
   for (auto it = start; it != end; ++it) {
      const auto& [sequence_diff_key, sequence_diff] = *it;
      RHYDB_ASSERT(!sequence_diff.empty());

      if (std::find(symbols.begin(), symbols.end(), sequence_diff_key.symbol) == symbols.end()) {
         continue;
      }
      result.emplace_back(
         sequence_diff_key.v_index, roaring_util::RoaringContainerView{sequence_diff}
      );
   }
   return result;
}

using rhydb::roaring_util::roaringSubsetRanks;

template <typename SymbolType>
void VerticalSequenceIndex<SymbolType>::overwriteSymbolsInSequences(
   std::vector<std::string>& sequences,
   const Bitmap& row_ids
) const {
   RHYDB_ASSERT_EQ(sequences.size(), row_ids.cardinality());
   if (row_ids.isEmpty()) {
      return;
   }
   // The blocks come in ascending key order, so the last one carries the largest v_index.
   size_t max_v_index = 0;
   for (const auto& [key, view] : row_ids) {
      max_v_index = key;
   }

   // Construct the slicesarrays that correspond to individual roaring containers = v_index
   std::vector<std::string*> sequences_by_v_index(max_v_index + 1);
   std::vector<size_t> sequences_by_v_index_sizes(max_v_index + 1);
   std::vector<const roaring::internal::container_t*> roaring_containers_by_v_index(
      max_v_index + 1
   );
   std::vector<uint8_t> roaring_typecodes_by_v_index(max_v_index + 1);
   std::string* current_sequences_pointer = sequences.data();

   for (const auto& [key, view] : row_ids) {
      RHYDB_ASSERT(key <= max_v_index);
      sequences_by_v_index.at(key) = current_sequences_pointer;
      sequences_by_v_index_sizes.at(key) = view.getCardinality();
      roaring_containers_by_v_index.at(key) = view.rawContainer();
      roaring_typecodes_by_v_index.at(key) = view.getTypecode();
      current_sequences_pointer += view.getCardinality();
   }

   for (const auto& [sequence_diff_key, sequence_diff] : vertical_bitmaps) {
      const uint16_t v_index = sequence_diff_key.v_index;

      if (v_index > max_v_index || sequences_by_v_index_sizes.at(v_index) == 0) {
         continue;
      }

      auto ranks_in_reconstructed_sequences = roaringSubsetRanks(
         roaring_containers_by_v_index.at(v_index),
         roaring_typecodes_by_v_index.at(v_index),
         sequence_diff.rawContainer(),
         sequence_diff.getTypecode(),
         /*base=*/0  // Base rank is 0, because we have a slice per container
      );

      // Ranks are ordered, back() = largest rank -> should be a valid size for the current sequence
      // slice
      RHYDB_ASSERT(
         ranks_in_reconstructed_sequences.empty() ||
         ranks_in_reconstructed_sequences.back() <= sequences_by_v_index_sizes.at(v_index)
      );

      auto& current_v_index_sequences = sequences_by_v_index.at(v_index);
      for (auto rank_in_reconstructed_sequences : ranks_in_reconstructed_sequences) {
         // Ranks are 1-indexed
         const uint32_t id_in_reconstructed_sequences = rank_in_reconstructed_sequences - 1;
         current_v_index_sequences[id_in_reconstructed_sequences].at(sequence_diff_key.position) =
            SymbolType::symbolToChar(sequence_diff_key.symbol);
      }
   },
};

template class VerticalSequenceIndex<Nucleotide>;
template class VerticalSequenceIndex<AminoAcid>;

std::vector<std::pair<uint16_t, std::vector<uint16_t>>> splitIdsIntoBatches(
   const std::vector<uint32_t>& sorted_ids
) {
   std::vector<std::pair<uint16_t, std::vector<uint16_t>>> ids_in_batches;

   if (sorted_ids.empty()) {
      return ids_in_batches;
   }

   uint16_t current_upper_bits = sorted_ids.front() >> 16;
   ids_in_batches.emplace_back(current_upper_bits, std::vector<uint16_t>{});

   for (const uint32_t sorted_id : sorted_ids) {
      const uint16_t upper_bits = sorted_id >> 16;
      const uint16_t lower_bits = sorted_id & 0xFFFF;

      // If the upper bits changed, start a new batch
      if (upper_bits != current_upper_bits) {
         current_upper_bits = upper_bits;
         ids_in_batches.emplace_back(upper_bits, std::vector<uint16_t>{});
      }

      // Add the lower bits to the current batch
      ids_in_batches.back().second.push_back(lower_bits);
   }

   return ids_in_batches;
}

}  // namespace rhydb::storage::column
