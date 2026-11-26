#include "rhydb/storage/sequence/sequence_chunk.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_compaction.h"

namespace rhydb::storage::sequence {

namespace {

template <typename SymbolType>
void fillAlphabet(
   std::array<char, SequenceReference::MAX_SYMBOLS>& symbol_chars,
   uint8_t& num_symbols,
   uint8_t& missing_symbol
) {
   static_assert(SymbolType::COUNT <= SequenceReference::MAX_SYMBOLS);
   num_symbols = static_cast<uint8_t>(SymbolType::COUNT);
   for (const auto symbol : SymbolType::SYMBOLS) {
      symbol_chars.at(static_cast<uint8_t>(symbol)) = SymbolType::symbolToChar(symbol);
   }
   missing_symbol = static_cast<uint8_t>(SymbolType::SYMBOL_MISSING);
}

template <typename SymbolType>
std::vector<uint8_t> symbolsOf(std::string_view text) {
   std::vector<uint8_t> symbols;
   symbols.reserve(text.size());
   for (const char character : text) {
      const auto symbol = SymbolType::charToSymbol(character);
      if (!symbol.has_value()) {
         throw std::invalid_argument(
            fmt::format(
               "illegal {} character '{}' in a reference",
               SymbolType::SYMBOL_NAME_LOWER_CASE,
               character
            )
         );
      }
      symbols.push_back(static_cast<uint8_t>(*symbol));
   }
   return symbols;
}

template <typename SymbolType>
CoverageAndMutations<SymbolType> diffAgainst(
   std::string_view sequence,
   uint32_t offset,
   const std::string& reference
) {
   if (sequence.size() + offset > reference.size()) {
      throw std::invalid_argument(
         fmt::format(
            "a sequence of length {} at offset {} does not fit into a reference of length {}",
            sequence.size(),
            offset,
            reference.size()
         )
      );
   }
   auto result = extractCoverageAndMutationsFromSequence<SymbolType>(
      sequence, offset, reference, referenceContainsMissingSymbol<SymbolType>(reference)
   );
   if (!result.has_value()) {
      throw std::invalid_argument(result.error());
   }
   return std::move(result).value();
}

void assertNotNull(const std::shared_ptr<const common::Buffer>& buffer) {
   RHYDB_ASSERT(buffer != nullptr);
}

std::shared_ptr<const common::Buffer> share(common::Buffer buffer) {
   return std::make_shared<const common::Buffer>(std::move(buffer));
}

}  // namespace

SequenceReference::SequenceReference(SequenceAlphabet alphabet, std::string text)
    : alphabet_(alphabet),
      text_(std::move(text)) {
   if (alphabet == SequenceAlphabet::NUCLEOTIDE) {
      fillAlphabet<Nucleotide>(symbol_chars, num_symbols, missing_symbol);
      symbols_ = symbolsOf<Nucleotide>(text_);
   } else {
      fillAlphabet<AminoAcid>(symbol_chars, num_symbols, missing_symbol);
      symbols_ = symbolsOf<AminoAcid>(text_);
   }
}

std::shared_ptr<const SequenceReference> SequenceReference::withSymbolsReplaced(
   std::span<const std::pair<uint32_t, uint8_t>> replacements
) const {
   std::string text = text_;
   for (const auto& [position, symbol] : replacements) {
      RHYDB_ASSERT_LT(symbol, num_symbols);
      text.at(position) = symbol_chars[symbol];
   }
   return std::make_shared<const SequenceReference>(alphabet_, std::move(text));
}

SequenceChunk::SequenceChunk(
   std::shared_ptr<const SequenceReference> reference,
   std::shared_ptr<const common::Buffer> mutations,
   std::shared_ptr<const common::Buffer> coverage,
   std::shared_ptr<const roaring_util::RoaringContainer> null_rows
)
    : reference_(std::move(reference)),
      mutations_buffer(std::move(mutations)),
      coverage_buffer(std::move(coverage)),
      null_rows_(std::move(null_rows)) {
   RHYDB_ASSERT(reference_ != nullptr);
   assertNotNull(mutations_buffer);
   assertNotNull(coverage_buffer);
   RHYDB_ASSERT(null_rows_ != nullptr);
   if constexpr (RHYDB_DEBUG_ASSERTIONS) {
      validate();
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SequenceChunk::validate() const {
   auto fail = [](std::string_view reason) {
      throw std::runtime_error(fmt::format("invalid sequence chunk: {}", reason));
   };
   const auto entries = VerticalSequenceIndex::fromBuffer(*mutations_buffer).entries();
   const auto coverage = HorizontalCoverageIndex::fromBuffer(*coverage_buffer);
   if (!entries.empty() && entries.back().position >= reference_->length()) {
      fail(
         fmt::format(
            "a symbol is stored at position {} of a reference of length {}",
            entries.back().position,
            reference_->length()
         )
      );
   }
   for (const auto& entry : entries) {
      if (entry.symbol >= reference_->numSymbols()) {
         fail(fmt::format("the symbol id {} is not a symbol of the alphabet", entry.symbol));
      }
   }
   if (coverage.numRows() > 0 && coverage.maxEnd() > reference_->length()) {
      fail(
         fmt::format(
            "a row is covered up to {}, beyond the reference of length {}",
            coverage.maxEnd(),
            reference_->length()
         )
      );
   }
   for (const uint16_t row : nullRows()) {
      if (row >= coverage.numRows()) {
         fail(fmt::format("the null row {} is not a row of the {} rows", row, coverage.numRows()));
      }
      const auto [start, end] = coverage.coverageRange(row);
      if (start != end) {
         fail(fmt::format("the null row {} covers [{}, {})", row, start, end));
      }
   }
}

SequenceChunk SequenceChunk::withMutations(
   std::shared_ptr<const SequenceReference> reference,
   std::shared_ptr<const common::Buffer> mutations
) const {
   return SequenceChunk{std::move(reference), std::move(mutations), coverage_buffer, null_rows_};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
SequenceChunk SequenceChunk::rebasedOnto(std::shared_ptr<const SequenceReference> reference) const {
   RHYDB_ASSERT(reference != nullptr);
   if (reference == reference_) {
      return *this;
   }
   RHYDB_ASSERT(reference->alphabet() == reference_->alphabet());
   RHYDB_ASSERT_EQ(reference->length(), reference_->length());

   const auto mutations = this->mutations();
   const auto coverage = this->coverage();
   std::vector<VerticalSequenceIndex::PositionReplacement> replacements;
   // Owns the containers the replacements view
   std::deque<roaring_util::RoaringContainer> old_reference_rows;
   for (uint32_t position = 0; position < reference->length(); ++position) {
      const uint8_t old_symbol = reference_->symbolAt(position);
      const uint8_t new_symbol = reference->symbolAt(position);
      if (old_symbol == new_symbol) {
         continue;
      }
      const auto entries = mutations.entriesAtPosition(position);
      std::vector<roaring_util::RoaringContainerView> stored;
      bool stores_new_symbol = false;
      for (const auto& entry : entries) {
         stored.push_back(mutations.container(entry));
         stores_new_symbol = stores_new_symbol || entry.symbol == new_symbol;
      }
      // The rows carrying the old reference symbol: covered, and storing no other symbol
      roaring_util::RoaringContainer carrying_old_symbol = coverage.coveredRows(position);
      if (!stored.empty() && !carrying_old_symbol.empty()) {
         const auto any_stored = roaring_util::fastUnion(stored);
         carrying_old_symbol -= roaring_util::RoaringContainerView{any_stored};
      }
      if (!stores_new_symbol && carrying_old_symbol.empty()) {
         continue;
      }

      VerticalSequenceIndex::PositionReplacement replacement{
         .position = position,
         .containers = {},
      };
      for (const auto& entry : entries) {
         if (entry.symbol != new_symbol) {
            replacement.containers.emplace_back(entry.symbol, mutations.container(entry));
         }
      }
      if (!carrying_old_symbol.empty()) {
         carrying_old_symbol.runOptimizeAndShrink();
         old_reference_rows.push_back(std::move(carrying_old_symbol));
         replacement.containers.emplace_back(
            old_symbol, roaring_util::RoaringContainerView{old_reference_rows.back()}
         );
      }
      std::ranges::sort(replacement.containers, {}, [](const auto& symbol_and_rows) {
         return symbol_and_rows.first;
      });
      replacements.push_back(std::move(replacement));
   }
   if (replacements.empty()) {
      return withMutations(std::move(reference), mutations_buffer);
   }
   return withMutations(std::move(reference), share(mutations.withPositionsReplaced(replacements)));
}

SequenceChunk SequenceChunk::compacted(
   const column::RowCompaction& compaction,
   uint16_t chunk_id
) const {
   RHYDB_ASSERT(compaction.newChunkId(chunk_id).has_value());
   if (!compaction.losesSomeRows(chunk_id)) {
      return *this;
   }

   const auto mutations = this->mutations();
   const auto coverage = this->coverage();
   VerticalSequenceIndexBuilder mutations_builder;
   for (const auto& entry : mutations.entries()) {
      const auto rows = compaction.compacted(chunk_id, mutations.container(entry));
      mutations_builder.add(entry.position, entry.symbol, roaring_util::RoaringContainerView{rows});
   }

   // The remaining rows keep their order, so they are added in their new order
   HorizontalCoverageIndexBuilder coverage_builder;
   for (uint32_t row = 0; row < coverage.numRows(); ++row) {
      const column::RowId row_id{.chunk_id = chunk_id, .row_in_chunk = static_cast<uint16_t>(row)};
      if (!compaction.map(row_id).has_value()) {
         continue;
      }
      const auto [start, end] = coverage.coverageRange(static_cast<uint16_t>(row));
      coverage_builder.addRow(start, end, coverage.missingRanges(static_cast<uint16_t>(row)));
   }

   auto null_rows = compaction.compacted(chunk_id, nullRows());
   return SequenceChunk{
      reference_,
      share(std::move(mutations_builder).finish()),
      share(std::move(coverage_builder).finish()),
      std::make_shared<const roaring_util::RoaringContainer>(std::move(null_rows)),
   };
}

size_t SequenceChunk::mutationsSizeInBytes() const {
   return mutations().containersSizeInBytes();
}

size_t SequenceChunk::missingSizeInBytes() const {
   return coverage().missingSizeInBytes();
}

SequenceChunkBuilder::SequenceChunkBuilder(std::shared_ptr<const SequenceReference> reference)
    : reference(std::move(reference)) {
   RHYDB_ASSERT(this->reference != nullptr);
}

void SequenceChunkBuilder::addRow(
   const Coverage& coverage,
   std::span<const std::pair<uint32_t, uint8_t>> mutations
) {
   const uint32_t row = numRows();
   this->coverage.addRow(coverage);
   if (mutations.empty()) {
      return;
   }
   if (mutations_by_position.empty()) {
      mutations_by_position.resize(reference->length());
   }
   for (const auto& [position, symbol] : mutations) {
      RHYDB_ASSERT_LT(position, reference->length());
      RHYDB_ASSERT_LT(symbol, reference->numSymbols());
      RHYDB_ASSERT(symbol != reference->symbolAt(position));
      mutations_by_position[position].push_back((uint32_t{symbol} << 16U) | row);
   }
}

void SequenceChunkBuilder::addRow(
   uint32_t start,
   uint32_t end,
   std::span<const MissingRange> missing_ranges,
   std::span<const std::pair<uint32_t, uint8_t>> mutations
) {
   const uint32_t row = numRows();
   coverage.addRow(start, end, missing_ranges);
   if (mutations.empty()) {
      return;
   }
   if (mutations_by_position.empty()) {
      mutations_by_position.resize(reference->length());
   }
   for (const auto& [position, symbol] : mutations) {
      RHYDB_ASSERT_LT(position, reference->length());
      RHYDB_ASSERT_LT(symbol, reference->numSymbols());
      mutations_by_position[position].push_back((uint32_t{symbol} << 16U) | row);
   }
}

void SequenceChunkBuilder::addText(std::string_view sequence, uint32_t offset) {
   if (reference->alphabet() == SequenceAlphabet::NUCLEOTIDE) {
      const auto diff = diffAgainst<Nucleotide>(sequence, offset, reference->text());
      addRow(diff.coverage, diff.mutations);
   } else {
      const auto diff = diffAgainst<AminoAcid>(sequence, offset, reference->text());
      addRow(diff.coverage, diff.mutations);
   }
}

void SequenceChunkBuilder::addNullRow() {
   null_rows.push_back(static_cast<uint16_t>(numRows()));
   coverage.addNullRow();
}

SequenceChunk SequenceChunkBuilder::finish() && {
   VerticalSequenceIndexBuilder mutations_builder;
   std::vector<uint16_t> rows;
   for (uint32_t position = 0; position < mutations_by_position.size(); ++position) {
      auto& symbols_and_rows = mutations_by_position[position];
      if (symbols_and_rows.empty()) {
         continue;
      }
      // Ordered by symbol, and by row within a symbol, as the index stores them
      std::ranges::sort(symbols_and_rows);
      size_t begin = 0;
      while (begin < symbols_and_rows.size()) {
         const auto symbol = static_cast<uint8_t>(symbols_and_rows[begin] >> 16U);
         rows.clear();
         size_t end = begin;
         for (; end < symbols_and_rows.size() && (symbols_and_rows[end] >> 16U) == symbol; ++end) {
            rows.push_back(static_cast<uint16_t>(symbols_and_rows[end]));
         }
         mutations_builder.add(position, symbol, rows);
         begin = end;
      }
      std::vector<uint32_t>{}.swap(symbols_and_rows);
   }

   auto null_container =
      roaring_util::RoaringContainer::withCapacity(static_cast<int32_t>(null_rows.size()));
   for (const uint16_t row : null_rows) {
      null_container.add(row);
   }
   null_container.runOptimizeAndShrink();

   return SequenceChunk{
      std::move(reference),
      share(std::move(mutations_builder).finish()),
      share(std::move(coverage).finish()),
      std::make_shared<const roaring_util::RoaringContainer>(std::move(null_container)),
   };
}

}  // namespace rhydb::storage::sequence
