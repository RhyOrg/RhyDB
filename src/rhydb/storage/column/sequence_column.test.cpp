#include "rhydb/storage/column/sequence_column.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/append/append_exception.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_layout.h"
#include "rhydb/storage/insertion_format_exception.h"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

using rhydb::Nucleotide;
using rhydb::append::AppendException;
using rhydb::storage::InsertionFormatException;
using rhydb::storage::column::SequenceColumn;
using rhydb::storage::column::SequenceColumnMetadata;

namespace {
// Buffers a single sequence into a chunk and applies it to the column. Parsing
// and validation happen inside appendChunk, so this is where errors are thrown.
void appendSequence(
   SequenceColumn<Nucleotide>& column,
   std::string_view sequence,
   uint32_t offset,
   const std::vector<std::string>& insertions
) {
   SequenceColumn<Nucleotide>::Builder builder(column.metadata, column.referenceText());
   builder.insert(sequence, offset, insertions);
   auto result = column.appendChunk(builder.finalize());
   RHYDB_ASSERT(result.has_value());
}

/// The rows (global ids) that store `symbol` at `position`
roaring::Roaring stored(
   const SequenceColumn<Nucleotide>& column,
   uint32_t position,
   Nucleotide::Symbol symbol
) {
   rhydb::storage::sequence::SymbolSet symbols;
   symbols.add(static_cast<uint8_t>(symbol));
   return rhydb::Bitmap::fromContainerViews(column.storedRowsWithSymbolsAt(position, symbols))
      .toRoaring();
}
}  // namespace

TEST(SequenceColumn, validErrorOnBadInsertionFormat_noTwoParts) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"A"}); },
      ThrowsMessage<InsertionFormatException>(::testing::HasSubstr(
         "Failed to parse insertion due to invalid format. Expected two parts "
         "(position and non-empty insertion value), instead got: 'A'"
      ))
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_firstPartNotANumber) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"A:G"}); },
      ThrowsMessage<InsertionFormatException>(::testing::HasSubstr(
         "Failed to parse insertion due to invalid format. Expected position "
         "that is parsable as an integer, instead got: 'A:G'"
      ))
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_secondPartIllegalSymbol) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() {
         appendSequence(under_test, "A", 0, {"0:EEEEE"});
         under_test.finalize();
      },
      ThrowsMessage<InsertionFormatException>(
         ::testing::HasSubstr("Illegal nucleotide character 'E' in insertion: 0:EEEEE")
      )
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_secondPartIsANumber) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"0:0"}); },
      ThrowsMessage<InsertionFormatException>(
         ::testing::HasSubstr("Illegal nucleotide character '0' in insertion: 0:0")
      )
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_secondPartEmpty) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"0:"}); },
      ThrowsMessage<InsertionFormatException>(::testing::HasSubstr(
         "Failed to parse insertion due to invalid format. Expected two parts "
         "(position and non-empty insertion value), instead got: '0:'"
      ))
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_firstPartEmpty) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {":A"}); },
      ThrowsMessage<InsertionFormatException>(::testing::HasSubstr(
         "Failed to parse insertion due to invalid format. Expected position "
         "that is parsable as an integer, instead got: ':A'"
      ))
   );
}

TEST(SequenceColumn, validErrorOnNegativeInsertionPosition) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"-5:G"}); },
      ThrowsMessage<InsertionFormatException>(::testing::HasSubstr("position must not be negative"))
   );
}

TEST(SequenceColumn, validErrorOnInsertionPositionOutOfRange) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"100:G"}); },
      ThrowsMessage<AppendException>(::testing::HasSubstr(
         "the insertion position (100) is larger than the length of the reference sequence (1)"
      ))
   );
}

TEST(SequenceColumn, validInsertionAtPositionZero) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   EXPECT_NO_THROW(appendSequence(under_test, "A", 0, {"0:G"}));
}

TEST(SequenceColumn, validInsertionAtPositionEqualToGenomeLength) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   EXPECT_NO_THROW(appendSequence(under_test, "A", 0, {"1:G"}));
}

namespace {

using rhydb::roaring_util::RoaringContainer;
using rhydb::roaring_util::RoaringContainerView;
using rhydb::storage::column::RowCompaction;
using rhydb::storage::column::RowLayout;
using rhydb::storage::sequence::SequenceReference;

std::vector<Nucleotide::Symbol> symbolsOf(std::string_view sequence) {
   std::vector<Nucleotide::Symbol> result;
   for (const char character : sequence) {
      result.push_back(Nucleotide::charToSymbol(character).value());
   }
   return result;
}

// Appends `sequences` (or null rows for "null") as one chunk and finalizes the column
template <typename SymbolType>
void appendChunk(SequenceColumn<SymbolType>& column, const std::vector<std::string>& sequences) {
   typename SequenceColumn<SymbolType>::Builder builder(column.metadata, column.referenceText());
   for (const auto& sequence : sequences) {
      if (sequence == "null") {
         builder.insertNull();
      } else {
         builder.insert(sequence, 0, std::vector<std::string>{});
      }
   }
   RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());
   column.finalize();
}

// Reconstructs every row of `column`, chunk by chunk
std::vector<std::string> reconstruct(const SequenceColumn<Nucleotide>& column) {
   std::vector<std::string> sequences;
   for (const auto& chunk : column.chunks()) {
      std::vector<std::string> texts(chunk->numRows(), std::string(chunk->sequenceLength(), '?'));
      std::vector<char*> pointers;
      pointers.reserve(texts.size());
      for (auto& text : texts) {
         pointers.push_back(text.data());
      }
      const auto rows = RoaringContainer::ofRange(0, chunk->numRows());
      rhydb::storage::sequence::reconstructSequences(*chunk, RoaringContainerView{rows}, pointers);
      sequences.insert(sequences.end(), texts.begin(), texts.end());
   }
   return sequences;
}

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceColumn, appendingLeavesTheStoredChunksUntouched) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGT")};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   appendChunk(under_test, {"AAGT", "AAGT", "AAGT", "ACGT"});
   const auto first_chunk = under_test.chunks()[0];
   const auto* const first_mutations = first_chunk->mutationsBuffer()->bytes().data();
   EXPECT_EQ(first_chunk->reference().text(), "AAGT");
   EXPECT_EQ(stored(under_test, 1, Nucleotide::Symbol::C), (roaring::Roaring{3}));

   appendChunk(under_test, {"ACGT", "ACGT", "ACGT", "ACGT"});
   appendChunk(under_test, {"ACGT", "AAGT"});

   ASSERT_EQ(under_test.numChunks(), 3);
   // The stored chunk is the same object, with the same buffers, as before the appends
   EXPECT_EQ(under_test.chunks()[0], first_chunk);
   EXPECT_EQ(under_test.chunks()[0]->mutationsBuffer()->bytes().data(), first_mutations);
   EXPECT_EQ(under_test.chunk(0).reference().text(), "AAGT");
   // The new chunks carry C at position 1 in most rows, the first chunk A
   EXPECT_EQ(under_test.chunk(1).sharedReference(), under_test.reference());
   EXPECT_EQ(under_test.chunk(2).sharedReference(), under_test.reference());
   EXPECT_EQ(stored(under_test, 1, Nucleotide::Symbol::A), (roaring::Roaring{(2U << 16U) + 1}));
   EXPECT_EQ(stored(under_test, 1, Nucleotide::Symbol::C), (roaring::Roaring{3}));
}

// Chunks over the reference ACGTA with different local references: the first keeps the reference
// (A and T tie at position 0), the second has T at position 0 in most rows, the third holds a null
// row, and the fourth has the second one's local reference, which it shares. The column is
// serialized and loaded again.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceColumn, reconstructsAndSerializesChunksWithDifferentLocalReferences) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGTA")};
   SequenceColumn<Nucleotide> original(&column_metadata);
   appendChunk(original, {"ACGTA", "TCGTA", "ACG-A", "TTGTA"});
   appendChunk(original, {"TCGGA", "TCGTA", "ACGTC"});
   appendChunk(original, {"null", "NACGN"});
   appendChunk(original, {"TCGTA"});

   ASSERT_EQ(original.numChunks(), 4);
   EXPECT_EQ(original.chunkSize(0), 4);
   EXPECT_EQ(original.chunkSize(1), 3);
   EXPECT_EQ(original.chunk(1).reference().text(), "TCGTA");
   // Each chunk stores the symbols that differ from its own local reference
   const uint32_t chunk_1 = 1U << 16U;
   EXPECT_EQ(stored(original, 0, Nucleotide::Symbol::A), (roaring::Roaring{chunk_1 + 2}));
   EXPECT_EQ(stored(original, 0, Nucleotide::Symbol::T), (roaring::Roaring{1, 3}));

   std::stringstream buffer;
   {
      boost::archive::binary_oarchive output_archive(buffer);
      output_archive << original;
   }
   SequenceColumn<Nucleotide> loaded(&column_metadata);
   {
      boost::archive::binary_iarchive input_archive(buffer);
      input_archive >> loaded;
   }
   for (const auto* column : {&original, &loaded}) {
      EXPECT_EQ(column->sequence_count, 10);
      EXPECT_EQ(column->chunk(0).sharedReference(), column->reference());
      EXPECT_EQ(column->chunk(2).reference().text(), "AACGA");
      // Chunks with equal local references share them
      EXPECT_EQ(column->chunk(3).sharedReference(), column->chunk(1).sharedReference());
      EXPECT_EQ(column->chunkReferences().size(), 3);
      // A local reference only holds its differences from the column's reference
      EXPECT_EQ(
         column->chunk(1).reference().differencesFrom(*column->reference()),
         (std::vector<SequenceReference::SymbolAt>{
            {0, static_cast<uint8_t>(Nucleotide::Symbol::T)},
         })
      );
      EXPECT_LE(
         column->chunk(1).reference().differencesSizeInBytes(), sizeof(SequenceReference::SymbolAt)
      );
      EXPECT_TRUE(column->isNull({.chunk_id = 2, .row_in_chunk = 0}));
      EXPECT_FALSE(column->isNull({.chunk_id = 2, .row_in_chunk = 1}));
      EXPECT_EQ(column->nullRows().toRoaring(), roaring::Roaring{2U << 16U});
      EXPECT_EQ(
         reconstruct(*column),
         (std::vector<std::string>{
            "ACGTA",
            "TCGTA",
            "ACG-A",
            "TTGTA",
            "TCGGA",
            "TCGTA",
            "ACGTC",
            "NNNNN",
            "NACGN",
            "TCGTA",
         })
      );
   }
   for (size_t chunk_id = 0; chunk_id < 4; ++chunk_id) {
      const auto& original_chunk = *original.chunks()[chunk_id];
      const auto& loaded_chunk = *loaded.chunks()[chunk_id];
      EXPECT_TRUE(
         std::ranges::equal(
            original_chunk.mutationsBuffer()->bytes(), loaded_chunk.mutationsBuffer()->bytes()
         )
      );
      EXPECT_TRUE(
         std::ranges::equal(
            original_chunk.coverageBuffer()->bytes(), loaded_chunk.coverageBuffer()->bytes()
         )
      );
      EXPECT_EQ(loaded_chunk.reference().text(), original_chunk.reference().text());
   }
   EXPECT_EQ(loaded.getInfo().vertical_bitmaps_size, original.getInfo().vertical_bitmaps_size);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceColumn, deleteRowsCompactsTheChunksThatLoseRows) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGTA")};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   appendChunk(under_test, {"ACGTA", "TCGTA", "ACG-A", "TTGTA"});
   appendChunk(under_test, {"TCGGA", "TCGTA", "ACGTC"});
   appendChunk(under_test, {"ACGTT"});
   const auto untouched_chunk = under_test.chunks()[2];
   const auto first_reference = under_test.chunk(0).sharedReference();

   // Delete rows 1 and 3 of chunk 0 and the whole chunk 1
   RowLayout layout;
   for (const uint32_t size : {4, 3, 1}) {
      layout.appendChunk(size);
   }
   const roaring::Roaring deleted{1, 3, 1U << 16U, (1U << 16U) + 1, (1U << 16U) + 2};
   under_test.deleteRows(RowCompaction{layout, rhydb::Bitmap{&deleted}});

   ASSERT_EQ(under_test.numChunks(), 2);
   EXPECT_EQ(under_test.chunkSize(0), 2);
   EXPECT_EQ(under_test.sequence_count, 3);
   // A compacted chunk keeps its local reference
   EXPECT_EQ(under_test.chunk(0).sharedReference(), first_reference);
   // A chunk that loses no rows is kept as it is
   EXPECT_EQ(under_test.chunks()[1], untouched_chunk);
   EXPECT_EQ(under_test.chunk(1).reference().text(), "ACGTT");
   EXPECT_EQ(reconstruct(under_test), (std::vector<std::string>{"ACGTA", "ACG-A", "ACGTT"}));
}

TEST(SequenceColumn, aMalformedInsertionLeavesTheColumnUnchanged) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGT")};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   SequenceColumn<Nucleotide>::Builder builder(under_test.metadata, under_test.referenceText());
   builder.insert("ACGT", 0, std::vector<std::string>{});
   builder.insert("ACGT", 0, std::vector<std::string>{"5:A"});
   const auto buffer = builder.finalize();
   EXPECT_THROW(
      { [[maybe_unused]] const auto result = under_test.appendChunk(buffer); }, AppendException
   );
   EXPECT_EQ(under_test.numChunks(), 0);
   EXPECT_EQ(under_test.sequence_count, 0);
}

TEST(SequenceColumn, choosesTheLocalReferenceOfAminoAcidChunks) {
   SequenceColumnMetadata<rhydb::AminoAcid> column_metadata{
      "test_column",
      {rhydb::AminoAcid::Symbol::M, rhydb::AminoAcid::Symbol::A, rhydb::AminoAcid::Symbol::Y},
   };
   SequenceColumn<rhydb::AminoAcid> under_test(&column_metadata);
   appendChunk(under_test, {"MKY", "MKX", "M*Y", "MAY"});
   appendChunk(under_test, {"MAY"});

   EXPECT_EQ(under_test.chunk(0).reference().text(), "MKY");
   EXPECT_EQ(under_test.chunk(1).sharedReference(), under_test.reference());
}

// The local reference of a chunk: at every position, the symbol most of its covered rows carry. A
// row that is null or has `N` at a position does not cover it, so it counts for no symbol; ties
// keep the reference, or else go to the first symbol in symbol order, and a position no row covers
// keeps the reference symbol. The rows not carrying the local reference symbol store their symbol.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceColumn, choosesTheMostCommonCoveredSymbolsAsLocalReference) {
   // The reference, the rows, the local reference, and the rows storing some symbols at a position
   using Case = std::tuple<
      std::string,
      std::vector<std::string>,
      std::string,
      uint32_t,
      std::vector<std::pair<char, roaring::Roaring>>>;
   const std::vector<Case> cases{
      {"C", {"A"}, "A", 0, {{'A', {}}, {'C', {}}}},
      {"C", {"A", "N", "N"}, "A", 0, {{'A', {}}, {'C', {}}}},
      {"C", {"A", "C"}, "C", 0, {{'A', {0}}}},
      {"T", {"A", "N", "T", "N", "T", "T", "A"}, "T", 0, {{'A', {0, 6}}}},
      {"T", {"A", "N", "T", "N", "A", "T", "A"}, "A", 0, {{'A', {}}, {'T', {2, 5}}}},
      // A and - both occur three times; - comes first in symbol order, so it wins.
      {
         "T",
         {"A", "N", "T", "N", "A", "-", "-", "-", "T", "A"},
         "-",
         0,
         {{'A', {0, 4, 9}}, {'T', {2, 8}}, {'-', {}}},
      },
      // At position 2, T is the most common symbol of the covered rows, but not of all rows.
      {
         "ACGT",
         {"ACTT", "ACTT", "ACTT", "ACNT", "ACNT", "ACGT"},
         "ACTT",
         2,
         {{'G', {5}}, {'T', {}}},
      },
      {"ACGT", {"null", "null"}, "ACGT", 0, {}},
      // Position 0 is covered by no row, position 3 is missing in every row
      {"ACGT", {"NTTN", "NTTN", "NGTN", "null"}, "ATTT", 1, {{'G', {2}}}},
   };
   for (const auto& [reference, sequences, local_reference, position, stored_rows] : cases) {
      SequenceColumnMetadata<Nucleotide> metadata{"test_column", symbolsOf(reference)};
      SequenceColumn<Nucleotide> column{&metadata};
      appendChunk(column, sequences);
      SCOPED_TRACE(::testing::PrintToString(sequences));
      EXPECT_EQ(column.chunk(0).reference().text(), local_reference);
      // The column's reference stays the reference sequence, and a chunk that keeps it shares it
      EXPECT_EQ(column.referenceText(), reference);
      if (local_reference == reference) {
         EXPECT_EQ(column.chunk(0).sharedReference(), column.reference());
      }
      for (const auto& [symbol, rows] : stored_rows) {
         EXPECT_EQ(stored(column, position, Nucleotide::charToSymbol(symbol).value()), rows)
            << "storing " << symbol;
      }
      auto expected = sequences;
      for (auto& sequence : expected) {
         sequence = sequence == "null" ? std::string(reference.size(), 'N') : sequence;
      }
      EXPECT_EQ(reconstruct(column), expected);
   }
}
