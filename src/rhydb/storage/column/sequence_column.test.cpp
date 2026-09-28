#include "rhydb/storage/column/sequence_column.h"

#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/append/append_exception.h"
#include "rhydb/storage/insertion_format_exception.h"

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
   SequenceColumn<Nucleotide>::Builder builder(
      column.metadata, column.local_reference_sequence_string
   );
   builder.insert(sequence, offset, insertions);
   auto result = column.appendChunk(builder.finalize());
   RHYDB_ASSERT(result.has_value());
}
}  // namespace

TEST(SequenceColumn, validErrorOnBadInsertionFormat_noTwoParts) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);

   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"A"}); },
      ThrowsMessage<InsertionFormatException>(
         ::testing::HasSubstr("Failed to parse insertion due to invalid format. Expected two parts "
                              "(position and non-empty insertion value), instead got: 'A'")
      )
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_firstPartNotANumber) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {"A:G"}); },
      ThrowsMessage<InsertionFormatException>(
         ::testing::HasSubstr("Failed to parse insertion due to invalid format. Expected position "
                              "that is parsable as an integer, instead got: 'A:G'")
      )
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
      ThrowsMessage<InsertionFormatException>(
         ::testing::HasSubstr("Failed to parse insertion due to invalid format. Expected two parts "
                              "(position and non-empty insertion value), instead got: '0:'")
      )
   );
}

TEST(SequenceColumn, validErrorOnBadInsertionFormat_firstPartEmpty) {
   SequenceColumnMetadata<Nucleotide> column_metadata{"test_column", {Nucleotide::Symbol::A}};
   SequenceColumn<Nucleotide> under_test(&column_metadata);
   EXPECT_THAT(
      // NOLINTNEXTLINE(clang-diagnostic-error)
      [&]() { appendSequence(under_test, "A", 0, {":A"}); },
      ThrowsMessage<InsertionFormatException>(
         ::testing::HasSubstr("Failed to parse insertion due to invalid format. Expected position "
                              "that is parsable as an integer, instead got: ':A'")
      )
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
         under_test.metadata, under_test.local_reference_sequence_string
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
         Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::T, Nucleotide::Symbol::T,
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
         under_test.metadata, under_test.local_reference_sequence_string
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
         Nucleotide::Symbol::A, Nucleotide::Symbol::A, Nucleotide::Symbol::G, Nucleotide::Symbol::T,
      })
   );

   // This returns nothing, because symbol A is equal to reference -> nothing should be stored
   ASSERT_EQ(
      under_test.vertical_sequence_index.getMatchingContainersAsBitmap(1, {Nucleotide::Symbol::A}),
      (roaring::Roaring{})
   );

   {
      SequenceColumn<Nucleotide>::Builder builder(
         under_test.metadata, under_test.local_reference_sequence_string
      );
      builder.insert("ACGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      builder.insert("ACGT", 0, std::vector<std::string>{});
      RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());
   }

   under_test.finalize();

   ASSERT_EQ(
      under_test.vertical_sequence_index.getMatchingContainersAsBitmap(1, {Nucleotide::Symbol::A}),
      (roaring::Roaring{0, 1, 2})
   );

   ASSERT_EQ(
      under_test.getLocalReference(),
      (std::vector<Nucleotide::Symbol>{
         Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T,
      })
   );
}
