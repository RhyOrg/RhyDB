#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "rhydb/common/panic.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/roaring_util/subset_ranks.h"

namespace rhydb::storage::sequence {

using roaring_util::CopyOnWriteContainer;
using roaring_util::RoaringContainer;
using roaring_util::RoaringContainerView;

namespace {

/// Calls `write(i)` for the `i`-th row of `rows` if it is in `stored`, for every `i`.
template <typename Write>
void forEachRankIn(RoaringContainerView rows, RoaringContainerView stored, const Write& write) {
   const auto ranks = roaring_util::roaringSubsetRanks(rows, stored, 0);
   for (const uint64_t rank : ranks) {
      // Ranks are 1-indexed
      write(static_cast<size_t>(rank - 1));
   }
}

RoaringContainer containerOf(const std::vector<uint16_t>& sorted_rows) {
   auto result = RoaringContainer::withCapacity(static_cast<int32_t>(sorted_rows.size()));
   for (const uint16_t row : sorted_rows) {
      result.add(row);
   }
   result.runOptimizeAndShrink();
   return result;
}

}  // namespace

void reconstructSequences(
   const SequenceChunk& chunk,
   RoaringContainerView rows,
   std::span<char* const> texts
) {
   RHYDB_ASSERT_EQ(texts.size(), rows.getCardinality());
   if (texts.empty()) {
      return;
   }
   const SequenceReference& reference = chunk.reference();
   const size_t length = reference.length();
   const std::string reference_text = reference.text();
   for (char* text : texts) {
      std::copy_n(reference_text.data(), length, text);
   }

   const auto& mutations = chunk.mutations();
   for (const auto& entry : mutations.entries()) {
      const char symbol = reference.symbolChar(entry.symbol);
      const uint32_t position = entry.position;
      forEachRankIn(rows, mutations.container(entry), [&](size_t index) {
         texts[index][position] = symbol;
      });
   }

   const char missing = reference.missingChar();
   const auto& coverage = chunk.coverage();
   size_t index = 0;
   for (const uint16_t row : rows) {
      char* text = texts[index++];
      const auto [start, end] = coverage.coverageRange(row);
      std::fill(text, text + start, missing);
      std::fill(text + end, text + length, missing);
      for (const auto& [begin, range_end] : coverage.missingRanges(row)) {
         std::fill(text + begin, text + range_end, missing);
      }
   }
}

void symbolsAt(
   const SequenceChunk& chunk,
   uint32_t position,
   RoaringContainerView rows,
   std::span<char> symbols
) {
   RHYDB_ASSERT_EQ(symbols.size(), rows.getCardinality());
   RHYDB_ASSERT_LT(position, chunk.sequenceLength());
   const SequenceReference& reference = chunk.reference();
   const auto& coverage = chunk.coverage();
   if (coverage.noRowCovers(position)) {
      std::ranges::fill(symbols, reference.missingChar());
      return;
   }
   std::ranges::fill(symbols, reference.symbolChar(reference.symbolAt(position)));

   const auto& mutations = chunk.mutations();
   for (const auto& entry : mutations.entriesAtPosition(position)) {
      const char symbol = reference.symbolChar(entry.symbol);
      forEachRankIn(rows, mutations.container(entry), [&](size_t index) {
         symbols[index] = symbol;
      });
   }

   if (coverage.everyRowCovers(position)) {
      return;
   }
   const char missing = reference.missingChar();
   size_t index = 0;
   for (const uint16_t row : rows) {
      if (!coverage.covers(row, position)) {
         symbols[index] = missing;
      }
      ++index;
   }
}

std::vector<std::pair<uint8_t, RoaringContainerView>> storedSymbolsAt(
   const SequenceChunk& chunk,
   uint32_t position
) {
   const auto& mutations = chunk.mutations();
   const auto entries = mutations.entriesAtPosition(position);
   std::vector<std::pair<uint8_t, RoaringContainerView>> result;
   result.reserve(entries.size());
   for (const auto& entry : entries) {
      result.emplace_back(entry.symbol, mutations.container(entry));
   }
   return result;
}

RoaringContainer coveredRows(const SequenceChunk& chunk, uint32_t position) {
   return chunk.coverage().coveredRows(position);
}

RoaringContainer coveredRows(
   const SequenceChunk& chunk,
   uint32_t position,
   RoaringContainerView rows
) {
   const auto& coverage = chunk.coverage();
   if (rows.empty() || coverage.noRowCovers(position)) {
      return RoaringContainer::withCapacity(1);
   }
   if (coverage.everyRowCovers(position)) {
      return rows.toOwning();
   }
   // Checking the rows one by one beats scanning all rows of the chunk for a small selection
   constexpr uint32_t ROWS_CHECKED_PER_SCANNED_ROW = 16;
   if (rows.getCardinality() * ROWS_CHECKED_PER_SCANNED_ROW < coverage.numRows()) {
      std::vector<uint16_t> covered;
      for (const uint16_t row : rows) {
         if (coverage.covers(row, position)) {
            covered.push_back(row);
         }
      }
      return containerOf(covered);
   }
   const RoaringContainer all_covered = coverage.coveredRows(position);
   return RoaringContainerView{all_covered} & rows;
}

RoaringContainer rowsWithSymbolsAt(
   const SequenceChunk& chunk,
   uint32_t position,
   SymbolSet symbols,
   RoaringContainerView rows
) {
   RHYDB_ASSERT_LT(position, chunk.sequenceLength());
   const SequenceReference& reference = chunk.reference();
   const auto stored = storedSymbolsAt(chunk, position);

   std::vector<RoaringContainerView> stored_in_set;
   std::vector<RoaringContainerView> all_stored;
   for (const auto& [symbol, stored_rows] : stored) {
      all_stored.push_back(stored_rows);
      if (symbols.contains(symbol)) {
         stored_in_set.push_back(stored_rows);
      }
   }
   const RoaringContainer stored_union = roaring_util::fastUnion(stored_in_set);
   RoaringContainer result = RoaringContainerView{stored_union} & rows;

   const bool includes_reference = symbols.contains(reference.symbolAt(position));
   const bool includes_missing = symbols.contains(reference.missingSymbol());
   if (!includes_reference && !includes_missing) {
      return result;
   }
   const RoaringContainer covered = coveredRows(chunk, position, rows);
   if (includes_reference) {
      // Covered and storing no other symbol
      const RoaringContainer any_stored = roaring_util::fastUnion(all_stored);
      const RoaringContainer covered_reference =
         RoaringContainerView{covered} - RoaringContainerView{any_stored};
      result |= RoaringContainerView{covered_reference};
   }
   if (includes_missing) {
      // Not covered, and not null
      RoaringContainer not_covered = rows - RoaringContainerView{covered};
      not_covered -= chunk.nullRows();
      result |= RoaringContainerView{not_covered};
   }
   return result;
}

namespace {

/// The difference array of the number of rows covering each position: adds +1 at the start and -1
/// at the end of the covered range of `row`, and the opposite for each of its missing ranges.
void addCoverageChanges(
   const HorizontalCoverageIndex& coverage,
   uint16_t row,
   std::span<int64_t> coverage_changes
) {
   const auto [start, end] = coverage.coverageRange(row);
   coverage_changes[start] += 1;
   coverage_changes[end] -= 1;
   for (const auto& [begin, range_end] : coverage.missingRanges(row)) {
      coverage_changes[begin] -= 1;
      coverage_changes[range_end] += 1;
   }
}

/// Adds the counts of the stored symbols, counted with `count_stored(entry, container)`, and of
/// the reference symbol, the covered rows (as `coverage_changes` gives them) storing no symbol.
template <typename CountStored>
void addCountsFromCoverageChanges(
   const SequenceChunk& chunk,
   std::span<const int64_t> coverage_changes,
   const CountStored& count_stored,
   SymbolCounts& counts
) {
   const SequenceReference& reference = chunk.reference();
   const auto& mutations = chunk.mutations();
   const auto entries = mutations.entries();
   const auto reference_symbols = reference.symbols();
   size_t entry_idx = 0;
   int64_t covered = 0;
   for (uint32_t position = 0; position < reference.length(); ++position) {
      covered += coverage_changes[position];
      int64_t reference_count = covered;
      for (; entry_idx < entries.size() && entries[entry_idx].position == position; ++entry_idx) {
         const auto& entry = entries[entry_idx];
         const uint32_t count = count_stored(entry, mutations.container(entry));
         counts.at(entry.symbol, position) += count;
         reference_count -= count;
      }
      RHYDB_ASSERT_GE(reference_count, int64_t{0});
      counts.at(reference_symbols[position], position) += static_cast<uint32_t>(reference_count);
   }
}

}  // namespace

void addSymbolCounts(const SequenceChunk& chunk, SymbolCounts& counts) {
   const SequenceReference& reference = chunk.reference();
   RHYDB_ASSERT_EQ(counts.sequenceLength(), reference.length());
   RHYDB_ASSERT_EQ(counts.numSymbols(), reference.numSymbols());
   const auto& coverage = chunk.coverage();
   std::vector<int64_t> coverage_changes(reference.length() + 1, 0);
   const auto starts = coverage.starts();
   const auto ends = coverage.ends();
   for (size_t row = 0; row < starts.size(); ++row) {
      coverage_changes[starts[row]] += 1;
      coverage_changes[ends[row]] -= 1;
   }
   coverage.forEachRowWithMissing([&](uint16_t /*row*/, std::span<const MissingRange> ranges) {
      for (const auto& [begin, end] : ranges) {
         coverage_changes[begin] -= 1;
         coverage_changes[end] += 1;
      }
   });
   addCountsFromCoverageChanges(
      chunk,
      coverage_changes,
      [](const VerticalSequenceIndexEntry& entry, RoaringContainerView /*container*/) {
         return entry.cardinality;
      },
      counts
   );
}

void addSymbolCounts(const SequenceChunk& chunk, RoaringContainerView rows, SymbolCounts& counts) {
   if (rows.getCardinality() == chunk.numRows()) {
      addSymbolCounts(chunk, counts);
      return;
   }
   const SequenceReference& reference = chunk.reference();
   RHYDB_ASSERT_EQ(counts.sequenceLength(), reference.length());
   RHYDB_ASSERT_EQ(counts.numSymbols(), reference.numSymbols());
   if (rows.empty()) {
      return;
   }
   const auto& coverage = chunk.coverage();
   std::vector<int64_t> coverage_changes(reference.length() + 1, 0);
   for (const uint16_t row : rows) {
      addCoverageChanges(coverage, row, coverage_changes);
   }
   addCountsFromCoverageChanges(
      chunk,
      coverage_changes,
      [&](const VerticalSequenceIndexEntry& /*entry*/, RoaringContainerView container) {
         return roaring_util::andCardinality(rows, container);
      },
      counts
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
SymbolGroups groupRowsBySymbolAt(
   const SequenceChunk& chunk,
   uint32_t position,
   RoaringContainerView rows
) {
   RHYDB_ASSERT_LT(position, chunk.sequenceLength());
   const SequenceReference& reference = chunk.reference();
   const auto& coverage = chunk.coverage();
   const uint8_t reference_symbol = reference.symbolAt(position);
   const uint8_t missing_symbol = reference.missingSymbol();
   const RoaringContainerView null_rows = chunk.nullRows();
   const auto stored = storedSymbolsAt(chunk, position);

   if (stored.empty()) {
      // No row covers the position -> every row is missing. A null row forms its own group, so
      // only collapse when the chunk has no null rows.
      if (null_rows.empty() && coverage.noRowCovers(position)) {
         return SymbolGroups{.every_row_symbol = missing_symbol};
      }
      // Every row covers the position -> every row has the reference symbol. (A null row empties
      // the covered envelope, so this never holds with null rows.)
      if (coverage.everyRowCovers(position)) {
         return SymbolGroups{.every_row_symbol = reference_symbol};
      }
   }

   SymbolGroups result;
   if (!null_rows.empty()) {
      result.null_rows = null_rows;
   }

   // The stored symbols except the missing one, and except the reference and the missing one. (The
   // reference symbol is never stored, so excluding it only mirrors the definition.)
   CopyOnWriteContainer except_missing;
   CopyOnWriteContainer except_reference_and_missing;
   std::optional<RoaringContainerView> stored_missing;
   for (const auto& [symbol, stored_rows] : stored) {
      if (symbol == missing_symbol) {
         stored_missing = stored_rows;
         continue;
      }
      except_missing |= stored_rows;
      if (symbol != reference_symbol) {
         except_reference_and_missing |= stored_rows;
      }
   }

   const RoaringContainer covered = coveredRows(chunk, position, rows);

   auto stored_rows_of = [&](uint8_t symbol) -> std::optional<RoaringContainerView> {
      const auto found =
         std::ranges::find(stored, symbol, &std::pair<uint8_t, RoaringContainerView>::first);
      if (found == stored.end()) {
         return std::nullopt;
      }
      return found->second;
   };

   for (uint8_t symbol = 0; symbol < reference.numSymbols(); ++symbol) {
      const bool is_reference = symbol == reference_symbol;
      const bool is_missing = symbol == missing_symbol;

      if (!is_reference && !is_missing) {
         // A stored symbol: the stored rows are handed out as they are, neither copied nor
         // intersected with the selection.
         if (const auto stored_rows = stored_rows_of(symbol)) {
            result.groups.emplace_back(symbol, CopyOnWriteContainer{*stored_rows});
         }
         continue;
      }

      CopyOnWriteContainer group;
      if (is_reference && is_missing) {
         // The reference symbol is the missing symbol: every selected row without another stored
         // symbol carries it, except the null rows.
         group = CopyOnWriteContainer{rows - except_missing.view()};
         group -= null_rows;
      } else if (is_missing) {
         // Not covered here ...
         group = CopyOnWriteContainer{rows - RoaringContainerView{covered}};
         if (stored_missing.has_value()) {
            // ... or storing the missing symbol ...
            const RoaringContainer selected_stored_missing = rows & *stored_missing;
            group |= RoaringContainerView{selected_stored_missing};
         }
         // ... but not null.
         group -= null_rows;
      } else {
         // Covered and storing no other symbol
         group = CopyOnWriteContainer{
            RoaringContainerView{covered} - except_reference_and_missing.view(),
         };
      }
      if (!group.empty()) {
         result.groups.emplace_back(symbol, std::move(group));
      }
   }
   return result;
}

}  // namespace rhydb::storage::sequence
