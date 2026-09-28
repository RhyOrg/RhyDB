#include "rhydb/common/aligned_sequence.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"

namespace rhydb {

template <typename SymbolType>
std::expected<CoverageAndMutations<SymbolType>, std::string>
extractCoverageAndMutationsFromSequence(
   std::string_view sequence,
   size_t offset,
   std::string_view reference,
   bool reference_is_missing_somewhere
) {
   Coverage coverage;
   Mutations<SymbolType> mutations;

   const char* const sequence_data = sequence.data();
   const char* const reference_data = reference.data() + offset;
   const size_t length = sequence.size();

   // lambda to process a single character from the input
   auto process_one = [&](size_t char_in_sequence) -> std::expected<void, std::string> {
      const auto position_idx = static_cast<uint32_t>(char_in_sequence + offset);
      const char character = sequence_data[char_in_sequence];
      const auto symbol = SymbolType::charToSymbol(character);
      if (!symbol.has_value()) {
         return std::unexpected{
            fmt::format(
               "illegal character '{}' at position {} in the input sequence",
               character,
               position_idx
            ),
         };
      }
      if (symbol == SymbolType::SYMBOL_MISSING) {
         coverage.missing_positions.push_back(position_idx);
      } else if (symbol != SymbolType::charToSymbol(reference_data[char_in_sequence])) {
         // The characters differ but may still encode the same symbol (e.g. lower case
         // input, or 'U' against a reference 'T'), which is not a mutation.
         mutations.mutations.emplace_back(position_idx, symbol.value());
      }
      return {};
   };

   size_t char_in_sequence = 0;
   if (!reference_is_missing_somewhere) {
      // Sequences deviate from the reference in only a handful of positions, so compare
      // them a word at a time and only look at the characters that actually differ.
      static_assert(
         std::endian::native == std::endian::little,
         "the lowest set bit of the xor of two words identifies the first differing character "
         "only on a little endian machine"
      );
      constexpr size_t WORD_SIZE = sizeof(uint64_t);
      for (; char_in_sequence + WORD_SIZE <= length; char_in_sequence += WORD_SIZE) {
         uint64_t sequence_word;
         uint64_t reference_word;
         std::memcpy(&sequence_word, sequence_data + char_in_sequence, WORD_SIZE);
         std::memcpy(&reference_word, reference_data + char_in_sequence, WORD_SIZE);
         uint64_t differing_bytes = sequence_word ^ reference_word;
         while (differing_bytes != 0) {
            const size_t byte_in_word = static_cast<size_t>(std::countr_zero(differing_bytes)) / 8;
            auto result = process_one(char_in_sequence + byte_in_word);
            if (!result.has_value()) {
               return std::unexpected{result.error()};
            }
            differing_bytes &= ~(static_cast<uint64_t>(0xFF) << (byte_in_word * 8));
         }
      }
   }
   for (; char_in_sequence < length; ++char_in_sequence) {
      if (reference_is_missing_somewhere ||
          sequence_data[char_in_sequence] != reference_data[char_in_sequence]) {
         auto result = process_one(char_in_sequence);
         if (!result.has_value()) {
            return std::unexpected{result.error()};
         }
      }
   }

   // The covered region spans from the first to the last non-missing position. Since the
   // missing positions are collected in ascending order, the ones outside that region are
   // exactly the leading and trailing ones. They carry no coverage information
   // (insertCoverage would trim them anyway), so they are dropped to keep the buffered
   // chunk as small as possible.
   const auto& missing_positions = coverage.missing_positions;
   size_t leading_missing = 0;
   while (leading_missing < missing_positions.size() &&
          missing_positions[leading_missing] == offset + leading_missing) {
      ++leading_missing;
   }
   size_t trailing_missing = 0;
   while (trailing_missing < missing_positions.size() - leading_missing &&
          missing_positions[missing_positions.size() - 1 - trailing_missing] ==
             offset + length - 1 - trailing_missing) {
      ++trailing_missing;
   }
   if (leading_missing + trailing_missing == length) {
      // Fully missing sequence: no covered region and nothing to record.
      coverage.missing_positions.clear();
   } else {
      coverage.start = static_cast<uint32_t>(offset + leading_missing);
      coverage.end = static_cast<uint32_t>(offset + length - trailing_missing);
      coverage.missing_positions.erase(
         coverage.missing_positions.end() - static_cast<ptrdiff_t>(trailing_missing),
         coverage.missing_positions.end()
      );
      coverage.missing_positions.erase(
         coverage.missing_positions.begin(),
         coverage.missing_positions.begin() + static_cast<ptrdiff_t>(leading_missing)
      );
   }
   return CoverageAndMutations<SymbolType>{coverage, mutations};
}

template <typename SymbolType>
std::expected<CoverageAndMutations<SymbolType>, std::string>
extractCoverageAndMutationsFromSequence(
   std::string_view sequence,
   size_t offset,
   std::string_view reference
) {
   return extractCoverageAndMutationsFromSequence<SymbolType>(
      sequence, offset, reference, referenceContainsMissingSymbol<SymbolType>(reference)
   );
}

template std::expected<CoverageAndMutations<Nucleotide>, std::string>
extractCoverageAndMutationsFromSequence<Nucleotide>(
   std::string_view,
   size_t,
   std::string_view,
   bool
);
template std::expected<CoverageAndMutations<AminoAcid>, std::string>
extractCoverageAndMutationsFromSequence<AminoAcid>(
   std::string_view,
   size_t,
   std::string_view,
   bool
);
template std::expected<CoverageAndMutations<Nucleotide>, std::string>
   extractCoverageAndMutationsFromSequence<Nucleotide>(std::string_view, size_t, std::string_view);
template std::expected<CoverageAndMutations<AminoAcid>, std::string>
   extractCoverageAndMutationsFromSequence<AminoAcid>(std::string_view, size_t, std::string_view);

}  // namespace rhydb
