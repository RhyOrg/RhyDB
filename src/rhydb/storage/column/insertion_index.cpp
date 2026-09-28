#include "rhydb/storage/column/insertion_index.h"

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>
#include <boost/container_hash/hash.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
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
            continuous_symbols.at(i), continuous_symbols.at(i + 1), continuous_symbols.at(i + 2),
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
std::unique_ptr<roaring::Roaring> InsertionPosition<SymbolType>::searchWithThreeMerIndex(
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
      return std::make_unique<roaring::Roaring>();
   }

   const auto cmp = [](const std::pair<it, it>& lhs_it_pair, const std::pair<it, it>& rhs_it_pair) {
      return *lhs_it_pair.first > *rhs_it_pair.first;
   };
   std::ranges::make_heap(min_heap, cmp);

   auto result = std::make_unique<roaring::Roaring>();

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
               *result |= insertion.row_ids;
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
         *result |= insertion.row_ids;
      }
   }

   return result;
}

template <typename SymbolType>
std::unique_ptr<roaring::Roaring> InsertionPosition<SymbolType>::searchWithRegex(
   const RE2& regex_search_pattern
) const {
   auto result = std::make_unique<roaring::Roaring>();
   for (const auto& [value, row_ids] : insertions) {
      if (RE2::FullMatch(value, regex_search_pattern)) {
         *result |= row_ids;
      }
   }
   return result;
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
std::unique_ptr<roaring::Roaring> InsertionPosition<SymbolType>::search(
   const std::string& search_pattern
) const {
   const auto search_three_mers = extractThreeMers<SymbolType>(search_pattern);
   const RE2 regex_search_pattern(search_pattern);

   if (!search_three_mers.empty()) {
      // We can only use the ThreeMerIndex if there is at least one 3-mer in the search pattern
      return searchWithThreeMerIndex(search_three_mers, regex_search_pattern);
   }
   return searchWithRegex(regex_search_pattern);
}

template <typename SymbolType>
void InsertionIndex<SymbolType>::addLazily(
   uint32_t position_idx,
   const std::string& insertion,
   const uint32_t row_id
) {
   auto& insertions_at_position =
      collected_insertions
         .emplace(position_idx, std::unordered_map<std::string, roaring::Roaring>{})
         .first->second;
   auto& sequence_ids_at_position =
      insertions_at_position.emplace(insertion, roaring::Roaring{}).first->second;
   sequence_ids_at_position.add(row_id);
}

template <typename SymbolType>
void InsertionIndex<SymbolType>::buildIndex() {
   insertion_positions.reserve(collected_insertions.size());

   for (auto [pos, insertion_info] : collected_insertions) {
      InsertionPosition<SymbolType> insertion_position;
      insertion_position.insertions.reserve(insertion_info.size());
      std::ranges::transform(
         insertion_info,
         std::back_inserter(insertion_position.insertions),
         [](auto& insertion) {
            return Insertion{std::move(insertion.first), std::move(insertion.second)};
         }
      );
      insertion_position.buildThreeMerIndex();
      insertion_positions.emplace(pos, std::move(insertion_position));
   }

   // free up the memory
   collected_insertions.clear();
}

template <typename SymbolType>
const std::unordered_map<uint32_t, InsertionPosition<SymbolType>>& InsertionIndex<
   SymbolType>::getInsertionPositions() const {
   return insertion_positions;
}

template <typename SymbolType>
std::unique_ptr<roaring::Roaring> InsertionIndex<SymbolType>::search(
   uint32_t position_idx,
   const std::string& search_pattern
) const {
   const auto insertion_pos_it = insertion_positions.find(position_idx);
   if (insertion_pos_it == insertion_positions.end()) {
      return std::make_unique<roaring::Roaring>();
   }
   return insertion_pos_it->second.search(search_pattern);
}

template class ThreeMerHash<Nucleotide>;
template class ThreeMerHash<AminoAcid>;

template class InsertionIndex<Nucleotide>;
template class InsertionIndex<AminoAcid>;

}  // namespace rhydb::storage::insertion
