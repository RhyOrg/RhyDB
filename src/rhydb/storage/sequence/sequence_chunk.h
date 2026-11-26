#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "rhydb/common/aligned_sequence.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/sequence/horizontal_coverage_index.h"
#include "rhydb/storage/sequence/vertical_sequence_index.h"

namespace rhydb {
class Nucleotide;
class AminoAcid;
}  // namespace rhydb

namespace rhydb::storage::column {
class RowCompaction;
}  // namespace rhydb::storage::column

namespace rhydb::storage::sequence {

/// The alphabet of a sequence column; selects the `SymbolType` (`Nucleotide` or `AminoAcid`)
/// whose `Symbol` values a sequence chunk stores as `uint8_t`.
enum class SequenceAlphabet : uint8_t { NUCLEOTIDE, AMINO_ACID };

template <typename SymbolType>
[[nodiscard]] constexpr SequenceAlphabet alphabetOf();

/// The local reference the rows of sequence chunks are stored against: a row stores only where its
/// text differs from this reference. It is immutable and shared (via `std::shared_ptr`) by all
/// chunks stored against it, e.g. by all chunks of a sequence column.
///
/// Symbols are addressed by their id, the `uint8_t` value of `SymbolType::Symbol`.
class SequenceReference {
  public:
   static constexpr size_t MAX_SYMBOLS = 32;

  private:
   SequenceAlphabet alphabet_;
   std::string text_;
   std::vector<uint8_t> symbols_;
   std::array<char, MAX_SYMBOLS> symbol_chars{};
   uint8_t num_symbols = 0;
   uint8_t missing_symbol = 0;

  public:
   /// Throws `std::invalid_argument` if `text` holds a character that is not a symbol of
   /// `alphabet`.
   SequenceReference(SequenceAlphabet alphabet, std::string text);

   template <typename SymbolType>
   [[nodiscard]] static std::shared_ptr<const SequenceReference> create(std::string text) {
      return std::make_shared<const SequenceReference>(alphabetOf<SymbolType>(), std::move(text));
   }

   [[nodiscard]] SequenceAlphabet alphabet() const { return alphabet_; }

   /// The reference as text, one character per position.
   [[nodiscard]] const std::string& text() const { return text_; }

   [[nodiscard]] size_t length() const { return text_.size(); }

   /// The id of the reference symbol at `position`.
   [[nodiscard]] uint8_t symbolAt(uint32_t position) const { return symbols_[position]; }

   /// The number of symbols of the alphabet; symbol ids are `< numSymbols()`.
   [[nodiscard]] uint8_t numSymbols() const { return num_symbols; }

   [[nodiscard]] char symbolChar(uint8_t symbol) const { return symbol_chars[symbol]; }

   /// The id of the symbol of a position a row does not cover (`SymbolType::SYMBOL_MISSING`).
   [[nodiscard]] uint8_t missingSymbol() const { return missing_symbol; }

   [[nodiscard]] char missingChar() const { return symbol_chars[missing_symbol]; }

   /// A copy of this reference with the symbol at each `(position, symbol)` replaced.
   [[nodiscard]] std::shared_ptr<const SequenceReference> withSymbolsReplaced(
      std::span<const std::pair<uint32_t, uint8_t>> replacements
   ) const;
};

/// A set of symbol ids (all `< SequenceReference::MAX_SYMBOLS`).
class SymbolSet {
   uint32_t mask = 0;

  public:
   SymbolSet() = default;

   template <typename SymbolType>
   [[nodiscard]] static SymbolSet of(std::span<const typename SymbolType::Symbol> symbols) {
      SymbolSet result;
      for (const auto symbol : symbols) {
         result.add(static_cast<uint8_t>(symbol));
      }
      return result;
   }

   void add(uint8_t symbol) { mask |= uint32_t{1} << symbol; }

   [[nodiscard]] bool contains(uint8_t symbol) const { return ((mask >> symbol) & 1U) != 0; }

   [[nodiscard]] bool empty() const { return mask == 0; }
};

/// The rows of one 2^16-row chunk of a sequence column: an immutable, self-contained value.
///
/// It bundles
///  - the chunk's `VerticalSequenceIndex` (for every position, the rows whose symbol differs from
///    the reference, one container per symbol),
///  - its `HorizontalCoverageIndex` (every row's covered range and the missing positions within),
///  - its null rows, and
///  - the `SequenceReference` the rows are stored against.
///
/// Rows are addressed by their index in the chunk (`uint16_t`). The text of a non-null row is the
/// reference, with each symbol stored in the vertical index for the row written over it, and the
/// missing symbol written over every position the row does not cover. A null row has an empty
/// covered range.
///
/// The indexes are pointer-free byte buffers and the null rows a roaring container, all behind
/// `std::shared_ptr`s, so copying a chunk only copies reference counts: the stored chunks of a
/// column can be shared with their readers without copying their buffers. Nothing mutates a
/// finished chunk; `SequenceChunkBuilder` creates new ones, and the `with...` functions return
/// changed copies that share the unchanged buffers.
class SequenceChunk {
   std::shared_ptr<const SequenceReference> reference_;
   VerticalSequenceIndex mutations_;
   HorizontalCoverageIndex coverage_;
   std::shared_ptr<const roaring_util::RoaringContainer> null_rows_;

  public:
   /// Checks that the parts fit together: the null rows are rows of the chunk with an empty covered
   /// range, and every position stored lies within the reference. Throws `std::runtime_error`
   /// otherwise.
   SequenceChunk(
      std::shared_ptr<const SequenceReference> reference,
      VerticalSequenceIndex mutations,
      HorizontalCoverageIndex coverage,
      std::shared_ptr<const roaring_util::RoaringContainer> null_rows
   );

   [[nodiscard]] uint32_t numRows() const { return coverage_.numRows(); }

   [[nodiscard]] const SequenceReference& reference() const { return *reference_; }

   [[nodiscard]] const std::shared_ptr<const SequenceReference>& sharedReference() const {
      return reference_;
   }

   /// The length of the text of every non-null row.
   [[nodiscard]] size_t sequenceLength() const { return reference_->length(); }

   [[nodiscard]] const VerticalSequenceIndex& mutations() const { return mutations_; }

   [[nodiscard]] const HorizontalCoverageIndex& coverage() const { return coverage_; }

   [[nodiscard]] roaring_util::RoaringContainerView nullRows() const {
      return roaring_util::RoaringContainerView{*null_rows_};
   }

   [[nodiscard]] bool isNull(uint16_t row) const { return null_rows_->contains(row); }

   /// A copy stored against `reference` with the vertical index `mutations`; the coverage and null
   /// rows are shared with this chunk.
   [[nodiscard]] SequenceChunk withMutations(
      std::shared_ptr<const SequenceReference> reference,
      VerticalSequenceIndex mutations
   ) const;

   /// The same rows stored against `reference` instead, which must have the same alphabet and
   /// length: wherever the two references differ, the rows carrying the old reference symbol come
   /// to store it, and the rows storing the new reference symbol no longer do. Shares the coverage
   /// and null rows with this chunk, and the vertical index too if nothing changes in it.
   [[nodiscard]] SequenceChunk rebasedOnto(
      std::shared_ptr<const SequenceReference> reference
   ) const;

   /// This chunk without the rows `compaction` deletes from chunk `chunk_id`, the remaining rows
   /// renumbered as `compaction` renumbers them. The chunk must keep some of its rows.
   [[nodiscard]] SequenceChunk compacted(
      const column::RowCompaction& compaction,
      uint16_t chunk_id
   ) const;

   /// Bytes of the vertical index's container payloads.
   [[nodiscard]] size_t mutationsSizeInBytes() const;

   /// Bytes of the missing positions in the coverage index.
   [[nodiscard]] size_t missingSizeInBytes() const;

   /// Writes the chunk without its reference (which chunks share, see `load`): the indexes as raw
   /// bytes, so loading them does not rebuild anything.
   template <class Archive>
   void save(Archive& archive) const {
      archive << mutations_;
      archive << coverage_;
      archive << *null_rows_;
   }

   /// Reads a chunk that `save` wrote, stored against `reference`.
   template <class Archive>
   [[nodiscard]] static SequenceChunk load(
      Archive& archive,
      std::shared_ptr<const SequenceReference> reference
   ) {
      VerticalSequenceIndex mutations;
      HorizontalCoverageIndex coverage;
      auto null_rows = roaring_util::RoaringContainer::withCapacity(0);
      archive >> mutations;
      archive >> coverage;
      archive >> null_rows;
      return SequenceChunk{
         std::move(reference),
         std::move(mutations),
         std::move(coverage),
         std::make_shared<const roaring_util::RoaringContainer>(std::move(null_rows)),
      };
   }
};

/// Builds a `SequenceChunk` row by row, in row order.
class SequenceChunkBuilder {
   std::shared_ptr<const SequenceReference> reference;
   HorizontalCoverageIndexBuilder coverage;
   /// Per position, `symbol << 16 | row` of every stored symbol, in row order. Sized lazily.
   std::vector<std::vector<uint32_t>> mutations_by_position;
   std::vector<uint16_t> null_rows;

  public:
   explicit SequenceChunkBuilder(std::shared_ptr<const SequenceReference> reference);

   /// Adds a row covering `coverage` with the `(position, symbol)` `mutations`, sorted by position,
   /// whose symbols differ from the reference symbol there. Missing positions outside the covered
   /// range are ignored.
   void addRow(const Coverage& coverage, std::span<const std::pair<uint32_t, uint8_t>> mutations);

   template <typename SymbolType>
   void addRow(const Coverage& coverage, const Mutations<SymbolType>& mutations) {
      std::vector<std::pair<uint32_t, uint8_t>> symbols;
      symbols.reserve(mutations.mutations.size());
      for (const auto& [position, symbol] : mutations.mutations) {
         symbols.emplace_back(position, static_cast<uint8_t>(symbol));
      }
      addRow(coverage, symbols);
   }

   /// Like `addRow(const Coverage&, ...)`, with the missing positions as ranges, as
   /// `HorizontalCoverageIndexBuilder::addRow` takes them.
   void addRow(
      uint32_t start,
      uint32_t end,
      std::span<const MissingRange> missing_ranges,
      std::span<const std::pair<uint32_t, uint8_t>> mutations
   );

   /// Adds the row whose text is `sequence` placed at `offset`, diffed against the reference: the
   /// positions before `offset` and after the sequence, and those holding the missing symbol, are
   /// not covered. Throws `std::invalid_argument` on an illegal character or a sequence that does
   /// not fit into the reference.
   void addText(std::string_view sequence, uint32_t offset = 0);

   void addNullRow();

   [[nodiscard]] uint32_t numRows() const { return coverage.numRows(); }

   [[nodiscard]] SequenceChunk finish() &&;
};

template <typename SymbolType>
constexpr SequenceAlphabet alphabetOf() {
   if constexpr (std::is_same_v<SymbolType, Nucleotide>) {
      return SequenceAlphabet::NUCLEOTIDE;
   } else {
      static_assert(std::is_same_v<SymbolType, AminoAcid>);
      return SequenceAlphabet::AMINO_ACID;
   }
}

}  // namespace rhydb::storage::sequence
