#include "rhydb/storage/column/insertion_index.h"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>
#include <boost/container_hash/hash.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/string_utils.h"
#include "rhydb/common/symbol_map.h"
#include "rhydb/storage/insertion_format_exception.h"

namespace rhydb::storage::insertion {

namespace {
constexpr std::string_view REGEX_ANY = ".*";

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> stringToSymbolVector(const std::string& sequence) {
   const size_t size = sequence.size();
   std::vector<typename SymbolType::Symbol> result;
   result.reserve(size);
   for (size_t i = 0; i < size; ++i) {
      if (i + 1 < size && sequence[i] == '\\') {
         ++i;
      }
      auto symbol = SymbolType::charToSymbol(sequence[i]);
      if (symbol == std::nullopt) {
         throw InsertionFormatException(
            fmt::format("Illegal nucleotide character '{}' in insertion: {}", sequence[i], sequence)
         );
      }
      result.emplace_back(*symbol);
   }
   return result;
}

template <typename SymbolType>
std::vector<std::array<typename SymbolType::Symbol, 3>> extractThreeMers(
   const std::string& search_pattern
) {
   std::unordered_set<std::array<typename SymbolType::Symbol, 3>, ThreeMerHash<SymbolType>> result;
   for (const auto& continuous_string : splitBy(search_pattern, REGEX_ANY)) {
      auto continuous_symbols = stringToSymbolVector<SymbolType>(continuous_string);
      for (size_t i = 0; (i + 2) < continuous_symbols.size(); i += 3) {
         const std::array<typename SymbolType::Symbol, 3> three_mer{
            continuous_symbols.at(i),
            continuous_symbols.at(i + 1),
            continuous_symbols.at(i + 2),
         };
         result.insert(three_mer);
      }
   }
   return {result.begin(), result.end()};
}
}  // namespace

template <typename SymbolType>
size_t ThreeMerHash<SymbolType>::operator()(
   const std::array<typename SymbolType::Symbol, 3>& three_mer
) const {
   size_t seed = 0;
   for (const auto one_mer : three_mer) {
      boost::hash_combine(seed, std::hash<typename SymbolType::Symbol>{}(one_mer));
   }
   return seed;
}

template <typename SymbolType>
InsertionSearchPattern<SymbolType>::InsertionSearchPattern(
   std::vector<std::array<typename SymbolType::Symbol, 3>> three_mers,
   std::unique_ptr<const RE2> regex
)
    : three_mers(std::move(three_mers)),
      regex(std::move(regex)) {}

template <typename SymbolType>
InsertionSearchPattern<SymbolType> InsertionSearchPattern<SymbolType>::make(
   const std::string& search_pattern
) {
   auto three_mers = extractThreeMers<SymbolType>(search_pattern);
   auto regex = std::make_unique<const RE2>(search_pattern);
   if (!regex->ok()) {
      throw InsertionFormatException(
         "Invalid regex in insertion search pattern '{}': {}", search_pattern, regex->error()
      );
   }
   return {std::move(three_mers), std::move(regex)};
}

template <typename SymbolType>
const std::vector<std::array<typename SymbolType::Symbol, 3>>& InsertionSearchPattern<
   SymbolType>::getThreeMers() const {
   return three_mers;
}

template <typename SymbolType>
const RE2& InsertionSearchPattern<SymbolType>::getRegex() const {
   return *regex;
}

template <typename SymbolType>
roaring_util::RoaringContainer InsertionPosition<SymbolType>::searchWithThreeMerIndex(
   const std::vector<std::array<typename SymbolType::Symbol, 3>>& search_three_mers,
   const RE2& search_pattern
) const {
   // We perform a k-way intersection between the candidate sets of insertion ids.
   // The candidate insertions are selected based on the 3-mers within the search pattern.
   // If an insertion id is in all candidate sets, then we have a viable candidate which
   // might match the search pattern. If this does not hold, there exists a continuous
   // 3-mer which is in the search pattern but not in the candidate insertion. Therefore, the
   // regex will never match and we can ignore this sequence.

   using it = InsertionIds::const_iterator;
   std::vector<std::pair<it, it>> min_heap;
   for (const auto& three_mer : search_three_mers) {
      if (three_mer_index.contains(three_mer)) {
         const auto& candidate_insertions = three_mer_index.at(three_mer);
         min_heap.emplace_back(candidate_insertions.cbegin(), candidate_insertions.cend());
      }
   }

   if (min_heap.size() < search_three_mers.size()) {
      return roaring_util::RoaringContainer::withCapacity(0);
   }

   const auto cmp = [](const std::pair<it, it>& lhs_it_pair, const std::pair<it, it>& rhs_it_pair) {
      return *lhs_it_pair.first > *rhs_it_pair.first;
   };
   std::ranges::make_heap(min_heap, cmp);

   std::vector<roaring_util::RoaringContainerView> matching_rows;

   size_t count = 0;
   uint32_t current_insertion_id = *min_heap.front().first;
   while (!min_heap.empty()) {
      const auto next_insertion_id = *min_heap.front().first;

      std::ranges::pop_heap(min_heap, cmp);
      ++min_heap.back().first;

      if (min_heap.back().first == min_heap.back().second) {
         min_heap.pop_back();
      } else {
         std::ranges::push_heap(min_heap, cmp);
      }

      if (next_insertion_id != current_insertion_id) {
         if (count == search_three_mers.size()) {
            const auto& insertion = insertions[current_insertion_id];
            if (RE2::FullMatch(insertion.value, search_pattern)) {
               matching_rows.emplace_back(insertion.row_ids);
            }
         }
         count = 1;
         current_insertion_id = next_insertion_id;
      } else {
         ++count;
      }
   }

   if (count == search_three_mers.size()) {
      const auto& insertion = insertions[current_insertion_id];
      if (RE2::FullMatch(insertion.value, search_pattern)) {
         matching_rows.emplace_back(insertion.row_ids);
      }
   }

   return roaring_util::fastUnion(matching_rows);
}

template <typename SymbolType>
roaring_util::RoaringContainer InsertionPosition<SymbolType>::searchWithRegex(
   const RE2& regex_search_pattern
) const {
   std::vector<roaring_util::RoaringContainerView> matching_rows;
   for (const auto& [value, row_ids] : insertions) {
      if (RE2::FullMatch(value, regex_search_pattern)) {
         matching_rows.emplace_back(row_ids);
      }
   }
   return roaring_util::fastUnion(matching_rows);
}

template <>
void InsertionPosition<Nucleotide>::buildThreeMerIndex() {
   using ThreeMersBitset =
      SymbolMap<Nucleotide, SymbolMap<Nucleotide, SymbolMap<Nucleotide, bool>>>;

   for (size_t insertion_id = 0; insertion_id < insertions.size(); ++insertion_id) {
      const auto& insertion_value = insertions[insertion_id].value;

      if (insertion_value.size() < 3) {
         continue;
      }

      const auto nuc_symbol_ids = stringToSymbolVector<Nucleotide>(insertion_value);

      ThreeMersBitset unique_three_mers{};
      for (size_t i = 0; i < (nuc_symbol_ids.size() - 2); ++i) {
         unique_three_mers[nuc_symbol_ids[i]][nuc_symbol_ids[i + 1]][nuc_symbol_ids[i + 2]] = true;
      }

      for (const Nucleotide::Symbol symbol1 : Nucleotide::SYMBOLS) {
         for (const Nucleotide::Symbol symbol2 : Nucleotide::SYMBOLS) {
            for (const Nucleotide::Symbol symbol3 : Nucleotide::SYMBOLS) {
               if (unique_three_mers[symbol1][symbol2][symbol3]) {
                  const std::array tuple{symbol1, symbol2, symbol3};
                  three_mer_index.emplace(tuple, InsertionIds{})
                     .first->second.push_back(insertion_id);
               }
            }
         }
      }
   }
}

template <>
void InsertionPosition<AminoAcid>::buildThreeMerIndex() {
   using ThreeMersBitset = SymbolMap<AminoAcid, SymbolMap<AminoAcid, SymbolMap<AminoAcid, bool>>>;

   for (size_t insertion_id = 0; insertion_id < insertions.size(); ++insertion_id) {
      const auto& insertion_value = insertions[insertion_id].value;

      if (insertion_value.size() < 3) {
         continue;
      }

      const auto aa_symbol_ids = stringToSymbolVector<AminoAcid>(insertion_value);

      ThreeMersBitset unique_three_mers{};
      for (size_t i = 0; i < (aa_symbol_ids.size() - 2); ++i) {
         unique_three_mers[aa_symbol_ids[i]][aa_symbol_ids[i + 1]][aa_symbol_ids[i + 2]] = true;
      }

      for (const AminoAcid::Symbol symbol1 : AminoAcid::SYMBOLS) {
         for (const AminoAcid::Symbol symbol2 : AminoAcid::SYMBOLS) {
            for (const AminoAcid::Symbol symbol3 : AminoAcid::SYMBOLS) {
               if (unique_three_mers[symbol1][symbol2][symbol3]) {
                  const std::array tuple{symbol1, symbol2, symbol3};
                  three_mer_index.emplace(tuple, InsertionIds{})
                     .first->second.push_back(insertion_id);
               }
            }
         }
      }
   }
}

template <typename SymbolType>
roaring_util::RoaringContainer InsertionPosition<SymbolType>::search(
   const InsertionSearchPattern<SymbolType>& search_pattern
) const {
   if (!search_pattern.getThreeMers().empty()) {
      // We can only use the ThreeMerIndex if there is at least one 3-mer in the search pattern
      return searchWithThreeMerIndex(search_pattern.getThreeMers(), search_pattern.getRegex());
   }
   return searchWithRegex(search_pattern.getRegex());
}

template <typename SymbolType>
InsertionIndexChunk<SymbolType> InsertionIndexChunk<SymbolType>::build(
   std::unordered_map<uint32_t, std::unordered_map<std::string, roaring_util::RoaringContainer>>&&
      rows_by_insertion
) {
   InsertionIndexChunk result;
   result.insertion_positions.reserve(rows_by_insertion.size());
   for (auto& [position_idx, rows_by_value] : rows_by_insertion) {
      InsertionPosition<SymbolType> insertion_position;
      insertion_position.insertions.reserve(rows_by_value.size());
      for (auto& [value, row_ids] : rows_by_value) {
         row_ids.runOptimizeAndShrink();
         insertion_position.insertions.push_back(
            Insertion{.value = value, .row_ids = std::move(row_ids)}
         );
      }
      insertion_position.buildThreeMerIndex();
      result.insertion_positions.emplace(position_idx, std::move(insertion_position));
   }
   return result;
}

template <typename SymbolType>
roaring_util::RoaringContainer InsertionIndexChunk<SymbolType>::search(
   uint32_t position_idx,
   const InsertionSearchPattern<SymbolType>& search_pattern
) const {
   const auto insertion_position = insertion_positions.find(position_idx);
   if (insertion_position == insertion_positions.end()) {
      return roaring_util::RoaringContainer::withCapacity(0);
   }
   return insertion_position->second.search(search_pattern);
}

template <typename SymbolType>
void InsertionIndexChunk<SymbolType>::deleteRows(
   uint16_t chunk_id,
   const column::RowCompaction& compaction
) {
   // The three-mer index refers to insertions by their position in `insertions`, so the insertions
   // are kept even when none of their rows remains
   for (auto& [position_idx, insertion_position] : insertion_positions) {
      for (auto& insertion : insertion_position.insertions) {
         compaction.compact(chunk_id, insertion.row_ids);
      }
   }
}

template <typename SymbolType>
void InsertionIndex<SymbolType>::add(
   uint32_t position_idx,
   const std::string& insertion,
   uint16_t row_in_chunk
) {
   auto& insertions_at_position = collected_insertions[position_idx];
   auto insertion_it = insertions_at_position.find(insertion);
   // Only allocate a container for an insertion not seen before in this chunk
   if (insertion_it == insertions_at_position.end()) {
      insertion_it =
         insertions_at_position.emplace(insertion, roaring_util::RoaringContainer::withCapacity(0))
            .first;
   }
   insertion_it->second.addChecked(row_in_chunk);
}

template <typename SymbolType>
void InsertionIndex<SymbolType>::appendChunk() {
   chunks.push_back(InsertionIndexChunk<SymbolType>::build(std::move(collected_insertions)));
   collected_insertions.clear();
}

template <typename SymbolType>
void InsertionIndex<SymbolType>::deleteRows(const column::RowCompaction& compaction) {
   RHYDB_ASSERT(collected_insertions.empty());
   for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      const auto old_chunk_id = static_cast<uint16_t>(chunk_id);
      if (compaction.losesSomeRows(old_chunk_id)) {
         chunks[chunk_id].deleteRows(old_chunk_id, compaction);
      }
   }
   compaction.dropDeletedChunks(chunks);
}

template <typename SymbolType>
Bitmap InsertionIndex<SymbolType>::search(
   uint32_t position_idx,
   const InsertionSearchPattern<SymbolType>& search_pattern
) const {
   return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
      return chunks[chunk_id].search(position_idx, search_pattern);
   });
}

template class ThreeMerHash<Nucleotide>;
template class ThreeMerHash<AminoAcid>;

template class InsertionSearchPattern<Nucleotide>;
template class InsertionSearchPattern<AminoAcid>;

template class InsertionIndexChunk<Nucleotide>;
template class InsertionIndexChunk<AminoAcid>;

template class InsertionIndex<Nucleotide>;
template class InsertionIndex<AminoAcid>;

}  // namespace rhydb::storage::insertion
