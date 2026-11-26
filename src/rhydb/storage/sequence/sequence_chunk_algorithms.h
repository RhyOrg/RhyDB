#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/sequence/sequence_chunk.h"

/// The algorithms on the rows of a single `SequenceChunk`. Each takes the chunk and, where it
/// works on a selection, the selected rows as a roaring container of row-in-chunk indexes. They
/// only need the chunk, not the sequence column it belongs to.
///
/// The selected rows may include null rows. A null row covers no position: its text (as
/// `reconstructSequences` writes it) is all missing symbols. Callers that distinguish null rows
/// consult `SequenceChunk::nullRows()`.
namespace rhydb::storage::sequence {

/// Writes the texts of the selected `rows`: `texts[i]` points to `chunk.sequenceLength()`
/// characters for the `i`-th row of `rows` (in ascending order).
void reconstructSequences(
   const SequenceChunk& chunk,
   roaring_util::RoaringContainerView rows,
   std::span<char* const> texts
);

/// Writes the character at `position` (< `chunk.sequenceLength()`) of the text of each selected
/// row to `symbols[i]`, for the `i`-th row of `rows` (in ascending order): the character
/// `reconstructSequences` writes there, without reconstructing the texts.
void symbolsAt(
   const SequenceChunk& chunk,
   uint32_t position,
   roaring_util::RoaringContainerView rows,
   std::span<char> symbols
);

/// The symbols stored at `position` and the rows (of all rows of the chunk) storing each, in
/// ascending symbol order: the rows whose text has that symbol, which differs from the reference
/// symbol there. Views into the chunk's buffers, valid as long as the chunk's buffers live.
[[nodiscard]] std::vector<std::pair<uint8_t, roaring_util::RoaringContainerView>> storedSymbolsAt(
   const SequenceChunk& chunk,
   uint32_t position
);

/// The selected non-null rows whose text has one of `symbols` at `position` (the missing symbol
/// included: a row that does not cover `position` has the missing symbol there).
[[nodiscard]] roaring_util::RoaringContainer rowsWithSymbolsAt(
   const SequenceChunk& chunk,
   uint32_t position,
   SymbolSet symbols,
   roaring_util::RoaringContainerView rows
);

/// The rows of the chunk that cover `position`.
[[nodiscard]] roaring_util::RoaringContainer coveredRows(
   const SequenceChunk& chunk,
   uint32_t position
);

/// The selected rows that cover `position`.
[[nodiscard]] roaring_util::RoaringContainer coveredRows(
   const SequenceChunk& chunk,
   uint32_t position,
   roaring_util::RoaringContainerView rows
);

/// Per symbol id and position, a number of rows.
class SymbolCounts {
   uint8_t num_symbols;
   size_t length;
   std::vector<uint32_t> counts;

  public:
   SymbolCounts(uint8_t num_symbols, size_t length)
       : num_symbols(num_symbols),
         length(length),
         counts(num_symbols * length, 0) {}

   [[nodiscard]] uint8_t numSymbols() const { return num_symbols; }

   [[nodiscard]] size_t sequenceLength() const { return length; }

   [[nodiscard]] uint32_t& at(uint8_t symbol, uint32_t position) {
      return counts[(symbol * length) + position];
   }

   [[nodiscard]] uint32_t at(uint8_t symbol, uint32_t position) const {
      return counts[(symbol * length) + position];
   }
};

/// Adds, for every position and symbol, the number of the chunk's rows that cover the position
/// and carry the symbol there (a symbol stored in the vertical index counts as such even if it is
/// the missing symbol). `counts` must be sized for the chunk's alphabet and sequence length.
void addSymbolCounts(const SequenceChunk& chunk, SymbolCounts& counts);

/// Like `addSymbolCounts(chunk, counts)`, counting only the selected `rows`.
void addSymbolCounts(
   const SequenceChunk& chunk,
   roaring_util::RoaringContainerView rows,
   SymbolCounts& counts
);

/// The rows of a chunk grouped by the symbol their text has at a position, see
/// `groupRowsBySymbolAt`.
struct SymbolGroups {
   /// Set if every row of the chunk has this symbol at the position (and none is null); `groups`
   /// is empty then.
   std::optional<uint8_t> every_row_symbol;
   /// The groups of non-null rows by symbol, in ascending symbol order, none of them empty.
   /// Intersected with the selected rows, they partition the selected non-null rows; a group may
   /// hold rows that are not selected (e.g. a view of the stored rows of a symbol). Views borrow
   /// the chunk's buffers.
   std::vector<std::pair<uint8_t, roaring_util::CopyOnWriteContainer>> groups;
   /// The chunk's null rows, if it has any (not intersected with the selected rows).
   std::optional<roaring_util::RoaringContainerView> null_rows;
};

/// Groups the selected `rows` by the symbol their text has at `position`, without materializing
/// what can be viewed: stored symbols are handed out as views into the chunk's buffers, and a
/// chunk whose rows all carry the same symbol is reported as such.
[[nodiscard]] SymbolGroups groupRowsBySymbolAt(
   const SequenceChunk& chunk,
   uint32_t position,
   roaring_util::RoaringContainerView rows
);

}  // namespace rhydb::storage::sequence
