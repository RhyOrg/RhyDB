#include "rhydb/storage/column/sequence_column.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
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
   SequenceColumn<Nucleotide>::Builder builder(column.metadata, column.localReferenceText());
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

// finalize decides whether to adapt the local reference from each position's coverage
// cardinality, which excludes rows whose base there is N. This checks that path end to end: at
// position 2 the majority of *covered* rows is T even though two rows have N there, so the
// reference must adapt G -> T.
TEST(SequenceColumn, adaptsLocalReferenceWhenMajorityOfCoveredRowsDiffers) {
   SequenceColumnMetadata<Nucleotide> column_metadata{
      "test_column",
      {Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T},
   };
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   {
      SequenceColumn<Nucleotide>::Builder builder(
         under_test.metadata, under_test.localReferenceText()
      );
      builder.insert("ACTT", 0, std::vector<std::string>{});  // T at position 2
      builder.insert("ACTT", 0, std::vector<std::string>{});  // T at position 2
      builder.insert("ACTT", 0, std::vector<std::string>{});  // T at position 2
      builder.insert("ACNT", 0, std::vector<std::string>{});  // N -> uncovered at position 2
      builder.insert("ACNT", 0, std::vector<std::string>{});  // N -> uncovered at position 2
      builder.insert("ACGT", 0, std::vector<std::string>{});  // G (== reference) at position 2
      RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());
   }

   under_test.finalize();

   // Position 2 has coverage cardinality 4 (the two N rows do not count): T occurs 3 times, G once,
   // so T wins. Every other position stays at the reference symbol.
   ASSERT_EQ(
      under_test.getLocalReference(),
      (std::vector<Nucleotide::Symbol>{
         Nucleotide::Symbol::A,
         Nucleotide::Symbol::C,
         Nucleotide::Symbol::T,
         Nucleotide::Symbol::T,
      })
   );
}

TEST(SequenceColumn, canFinalizeTwice) {
   SequenceColumnMetadata<Nucleotide> column_metadata{
      "test_column",
      {Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T},
   };
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   {
      SequenceColumn<Nucleotide>::Builder builder(
         under_test.metadata, under_test.localReferenceText()
      );
      builder.insert("AAGT", 0, std::vector<std::string>{});
      builder.insert("AAGT", 0, std::vector<std::string>{});
      builder.insert("AAGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());
   }

   under_test.finalize();

   ASSERT_EQ(
      under_test.getLocalReference(),
      (std::vector<Nucleotide::Symbol>{
         Nucleotide::Symbol::A,
         Nucleotide::Symbol::A,
         Nucleotide::Symbol::G,
         Nucleotide::Symbol::T,
      })
   );

   // This returns nothing, because symbol A is equal to reference -> nothing should be stored
   ASSERT_EQ(stored(under_test, 1, Nucleotide::Symbol::A), (roaring::Roaring{}));

   {
      SequenceColumn<Nucleotide>::Builder builder(
         under_test.metadata, under_test.localReferenceText()
      );
      builder.insert("ACGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());
   }

   under_test.finalize();

   ASSERT_EQ(stored(under_test, 1, Nucleotide::Symbol::A), (roaring::Roaring{0, 1, 2}));

   ASSERT_EQ(
      under_test.getLocalReference(),
      (std::vector<Nucleotide::Symbol>{
         Nucleotide::Symbol::A,
         Nucleotide::Symbol::C,
         Nucleotide::Symbol::G,
         Nucleotide::Symbol::T,
      })
   );
}

namespace {

using rhydb::roaring_util::RoaringContainer;
using rhydb::roaring_util::RoaringContainerView;
using rhydb::storage::column::RowCompaction;
using rhydb::storage::column::RowLayout;

const std::vector<std::vector<std::string>> TWO_CHUNKS_OF_SEQUENCES{
   {"ACGTA", "TCGTA", "ACG-A", "TTGTA"},
   {"TCGGA", "TCGTA", "ACGTC"},
};

const std::vector<std::string>
   ALL_SEQUENCES{"ACGTA", "TCGTA", "ACG-A", "TTGTA", "TCGGA", "TCGTA", "ACGTC"};

std::vector<Nucleotide::Symbol> symbolsOf(std::string_view sequence) {
   std::vector<Nucleotide::Symbol> result;
   for (const char character : sequence) {
      result.push_back(Nucleotide::charToSymbol(character).value());
   }
   return result;
}

// A column over the reference ACGTA holding `TWO_CHUNKS_OF_SEQUENCES`, one appended chunk per
// inner vector, finalized after each. Position 0 adapts from A to T once enough rows carry T.
void appendTwoChunks(SequenceColumn<Nucleotide>& column) {
   for (const auto& chunk : TWO_CHUNKS_OF_SEQUENCES) {
      SequenceColumn<Nucleotide>::Builder builder(column.metadata, column.localReferenceText());
      for (const auto& sequence : chunk) {
         builder.insert(sequence, 0, std::vector<std::string>{});
      }
      RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());
      column.finalize();
   }
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
TEST(SequenceColumn, reconstructsSequencesAcrossChunksAfterReferenceAdaptation) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGTA")};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   appendTwoChunks(under_test);

   EXPECT_EQ(under_test.localReferenceText(), "TCGTA");
   ASSERT_EQ(under_test.numChunks(), 2);
   EXPECT_EQ(under_test.chunkSize(0), 4);
   EXPECT_EQ(under_test.chunkSize(1), 3);
   // Every chunk is stored against the column's local reference
   for (const auto& chunk : under_test.chunks()) {
      EXPECT_EQ(chunk->sharedReference(), under_test.localReference());
   }
   // After the flip, A is stored as a mutation at position 0 in both chunks, and T is not stored.
   EXPECT_EQ(
      stored(under_test, 0, Nucleotide::Symbol::A), (roaring::Roaring{0, 2, (1U << 16) + 2})
   );
   EXPECT_EQ(stored(under_test, 0, Nucleotide::Symbol::T), roaring::Roaring{});
   EXPECT_EQ(reconstruct(under_test), ALL_SEQUENCES);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceColumn, serializationRoundTripsTheChunks) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGTA")};
   SequenceColumn<Nucleotide> original(&column_metadata);
   appendTwoChunks(original);
   {
      SequenceColumn<Nucleotide>::Builder builder(original.metadata, original.localReferenceText());
      builder.insertNull();
      builder.insert("ACG", 1, std::vector<std::string>{"2:AA"});
      RHYDB_ASSERT(original.appendChunk(builder.finalize()).has_value());
      original.finalize();
   }

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

   ASSERT_EQ(loaded.numChunks(), 3);
   EXPECT_EQ(loaded.localReferenceText(), original.localReferenceText());
   EXPECT_EQ(loaded.sequence_count, original.sequence_count);
   for (size_t chunk_id = 0; chunk_id < 3; ++chunk_id) {
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
      EXPECT_EQ(loaded_chunk.sharedReference(), loaded.localReference());
   }
   EXPECT_TRUE(loaded.isNull({.chunk_id = 2, .row_in_chunk = 0}));
   EXPECT_FALSE(loaded.isNull({.chunk_id = 2, .row_in_chunk = 1}));
   EXPECT_EQ(loaded.nullRows().toRoaring(), roaring::Roaring{2U << 16U});
   auto expected = ALL_SEQUENCES;
   expected.emplace_back("NNNNN");
   expected.emplace_back("NACGN");
   EXPECT_EQ(reconstruct(loaded), expected);
   EXPECT_EQ(loaded.getInfo().vertical_bitmaps_size, original.getInfo().vertical_bitmaps_size);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceColumn, deleteRowsCompactsTheChunksThatLoseRows) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGTA")};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   appendTwoChunks(under_test);
   {
      SequenceColumn<Nucleotide>::Builder builder(
         under_test.metadata, under_test.localReferenceText()
      );
      builder.insert("ACGTT", 0, std::vector<std::string>{});
      RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());
      under_test.finalize();
   }
   const auto untouched_chunk = under_test.chunks()[2];

   // Delete rows 1 and 3 of chunk 0 and the whole chunk 1
   RowLayout layout;
   layout.appendChunk(4);
   layout.appendChunk(3);
   layout.appendChunk(1);
   const roaring::Roaring deleted{1, 3, 1U << 16U, (1U << 16U) + 1, (1U << 16U) + 2};
   under_test.deleteRows(RowCompaction{layout, rhydb::Bitmap{&deleted}});

   ASSERT_EQ(under_test.numChunks(), 2);
   EXPECT_EQ(under_test.chunkSize(0), 2);
   EXPECT_EQ(under_test.chunkSize(1), 1);
   EXPECT_EQ(under_test.sequence_count, 3);
   // A chunk that loses no rows is kept as it is
   EXPECT_EQ(under_test.chunks()[1], untouched_chunk);
   EXPECT_EQ(reconstruct(under_test), (std::vector<std::string>{"ACGTA", "ACG-A", "ACGTT"}));
}

TEST(SequenceColumn, aMalformedInsertionLeavesTheColumnUnchanged) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", symbolsOf("ACGT")};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   SequenceColumn<Nucleotide>::Builder builder(
      under_test.metadata, under_test.localReferenceText()
   );
   builder.insert("ACGT", 0, std::vector<std::string>{});
   builder.insert("ACGT", 0, std::vector<std::string>{"5:A"});
   const auto buffer = builder.finalize();
   EXPECT_THROW(
      { [[maybe_unused]] const auto result = under_test.appendChunk(buffer); }, AppendException
   );
   EXPECT_EQ(under_test.numChunks(), 0);
   EXPECT_EQ(under_test.sequence_count, 0);
}

// Local reference adaptation at a single position, driven through `finalize` by real sequences.
// An `N` row does not cover the position, so it counts for no symbol.
namespace {

struct OnePositionColumn {
   SequenceColumnMetadata<Nucleotide> metadata;
   SequenceColumn<Nucleotide> column{&metadata};

   OnePositionColumn(char reference, const std::vector<std::string>& sequences)
       : metadata("test_column", {Nucleotide::charToSymbol(reference).value()}) {
      SequenceColumn<Nucleotide>::Builder builder(column.metadata, column.localReferenceText());
      for (const auto& sequence : sequences) {
         builder.insert(sequence, 0, std::vector<std::string>{});
      }
      RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());
      column.finalize();
   }

   [[nodiscard]] roaring::Roaring storedRows(Nucleotide::Symbol symbol) const {
      return stored(column, 0, symbol);
   }
};

}  // namespace

TEST(SequenceColumnReferenceAdaptation, adaptsToTheOnlyCoveredSymbol) {
   const OnePositionColumn under_test{'C', {"A"}};

   EXPECT_EQ(under_test.column.localReferenceText(), "A");
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::A), roaring::Roaring{});
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::C), roaring::Roaring{});
}

TEST(SequenceColumnReferenceAdaptation, adaptsToTheOnlyCoveredSymbolWhenOthersAreMissing) {
   const OnePositionColumn under_test{'C', {"A", "N", "N"}};

   EXPECT_EQ(under_test.column.localReferenceText(), "A");
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::A), roaring::Roaring{});
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::C), roaring::Roaring{});
}

TEST(SequenceColumnReferenceAdaptation, keepsTheReferenceOnATie) {
   const OnePositionColumn under_test{'C', {"A", "C"}};

   EXPECT_EQ(under_test.column.localReferenceText(), "C");
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::A), roaring::Roaring{0});
}

TEST(SequenceColumnReferenceAdaptation, keepsTheReferenceWhenItIsMostCommonAmongCoveredRows) {
   const OnePositionColumn under_test{'T', {"A", "N", "T", "N", "T", "T", "A"}};

   EXPECT_EQ(under_test.column.localReferenceText(), "T");
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::A), (roaring::Roaring{0, 6}));
}

TEST(SequenceColumnReferenceAdaptation, flipsStoredRowsToTheOldReference) {
   const OnePositionColumn under_test{'T', {"A", "N", "T", "N", "A", "T", "A"}};

   EXPECT_EQ(under_test.column.localReferenceText(), "A");
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::A), roaring::Roaring{});
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::T), (roaring::Roaring{2, 5}));
}

TEST(SequenceColumnReferenceAdaptation, flipsWithManySymbolsAndBreaksTiesBySymbolOrder) {
   // A and - both occur three times; - comes first in symbol order, so it wins.
   const OnePositionColumn under_test{'T', {"A", "N", "T", "N", "A", "-", "-", "-", "T", "A"}};

   EXPECT_EQ(under_test.column.localReferenceText(), "-");
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::A), (roaring::Roaring{0, 4, 9}));
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::T), (roaring::Roaring{2, 8}));
   EXPECT_EQ(under_test.storedRows(Nucleotide::Symbol::GAP), roaring::Roaring{});
}
