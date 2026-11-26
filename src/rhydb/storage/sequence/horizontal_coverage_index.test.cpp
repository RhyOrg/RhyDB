#include "rhydb/storage/sequence/horizontal_coverage_index.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/buffer.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::sequence {

namespace {

/// Plays the role the sequence column plays for the per-chunk indexes: records rows chunk by chunk
/// (in order, as ingestion does), builds one `HorizontalCoverageIndex` per chunk, and answers the
/// cross-chunk questions by asking every chunk.
class CoverageUnderTest {
   std::vector<std::vector<Coverage>> rows_by_chunk;
   mutable std::vector<common::Buffer> built_buffers;
   /// Views of `built_buffers`
   mutable std::vector<HorizontalCoverageIndex> built_chunks;
   mutable bool dirty = false;

  public:
   void insertCoverage(column::RowId row_id, const Coverage& coverage) {
      while (row_id.chunk_id >= rows_by_chunk.size()) {
         rows_by_chunk.emplace_back();
      }
      // Rows must be inserted in ascending order, chunk by chunk.
      RHYDB_ASSERT_EQ(static_cast<size_t>(row_id.chunk_id), rows_by_chunk.size() - 1);
      RHYDB_ASSERT_EQ(static_cast<size_t>(row_id.row_in_chunk), rows_by_chunk.back().size());
      rows_by_chunk.back().push_back(coverage);
      dirty = true;
   }

   void insertNullSequence(column::RowId row_id) {
      insertCoverage(row_id, Coverage{.start = 0, .end = 0, .missing_positions = {}});
   }

   [[nodiscard]] const std::vector<HorizontalCoverageIndex>& chunks() const {
      if (dirty) {
         built_chunks.clear();
         built_buffers.clear();
         for (const auto& rows : rows_by_chunk) {
            HorizontalCoverageIndexBuilder builder;
            for (const auto& coverage : rows) {
               builder.addRow(coverage);
            }
            built_buffers.push_back(std::move(builder).finish());
         }
         for (const auto& buffer : built_buffers) {
            built_chunks.push_back(HorizontalCoverageIndex::fromBuffer(buffer));
         }
         dirty = false;
      }
      return built_chunks;
   }

   /// The rows of all chunks covering `position`, as global row ids
   [[nodiscard]] roaring::Roaring coveredRows(uint32_t position) const {
      roaring::Roaring result;
      for (size_t chunk_id = 0; chunk_id < chunks().size(); ++chunk_id) {
         for (const uint16_t row :
              roaring_util::RoaringContainerView{chunks()[chunk_id].coveredRows(position)}) {
            result.add(
               column::RowId{.chunk_id = static_cast<uint16_t>(chunk_id), .row_in_chunk = row}
                  .toGlobal()
            );
         }
      }
      return result;
   }

   [[nodiscard]] std::vector<uint64_t> computeCoverageCardinalities(size_t genome_length) const {
      std::vector<const HorizontalCoverageIndex*> pointers;
      for (const auto& chunk : chunks()) {
         pointers.push_back(&chunk);
      }
      return sequence::computeCoverageCardinalities(pointers, genome_length);
   }

   [[nodiscard]] size_t numChunks() const { return chunks().size(); }

   [[nodiscard]] roaring_util::RoaringContainer coveredRowsInChunk(
      uint32_t position,
      size_t chunk_id
   ) const {
      if (chunk_id >= chunks().size()) {
         return roaring_util::RoaringContainer::withCapacity(1);
      }
      return chunks()[chunk_id].coveredRows(position);
   }

   [[nodiscard]] bool noRowCoversPositionInChunk(uint32_t position, size_t chunk_id) const {
      return chunk_id >= chunks().size() || chunks()[chunk_id].noRowCovers(position);
   }

   [[nodiscard]] bool positionCoveredByWholeChunk(uint32_t position, size_t chunk_id) const {
      return chunk_id < chunks().size() && chunks()[chunk_id].everyRowCovers(position);
   }

   template <typename Accessor>
   [[nodiscard]] std::vector<uint32_t> perChunk(Accessor accessor) const {
      std::vector<uint32_t> result;
      for (const auto& chunk : chunks()) {
         result.push_back(accessor(chunk));
      }
      return result;
   }

   [[nodiscard]] std::vector<uint32_t> batchMinStart() const {
      return perChunk([](const auto& chunk) { return chunk.minStart(); });
   }
   [[nodiscard]] std::vector<uint32_t> batchMaxStart() const {
      return perChunk([](const auto& chunk) { return chunk.maxStart(); });
   }
   [[nodiscard]] std::vector<uint32_t> batchMinEnd() const {
      return perChunk([](const auto& chunk) { return chunk.minEnd(); });
   }
   [[nodiscard]] std::vector<uint32_t> batchMaxEnd() const {
      return perChunk([](const auto& chunk) { return chunk.maxEnd(); });
   }
};

// Test fixture for HorizontalCoverageIndex with Nucleotide
class HorizontalCoverageIndexChunksTest : public ::testing::Test {
  protected:
   static constexpr size_t GENOME_LENGTH = 100;

   // A reference long enough to cover every sequence inserted in these tests (some
   // extend past GENOME_LENGTH). Only the coverage information is used here, so the
   // reference content is irrelevant - any sequence that differs only produces
   // mutations, which HorizontalCoverageIndex ignores.
   // NOLINTNEXTLINE(readability-identifier-naming)
   inline static const std::string REFERENCE = std::string(256, 'A');

   void SetUp() override { index = std::make_unique<CoverageUnderTest>(); }

   // Bridges the old per-sequence insertion API onto the new interface: derive the
   // Coverage from the sequence and offset and hand it to insertCoverage.
   void insertSequenceCoverage(std::string_view sequence, size_t offset) {
      index->insertCoverage(
         column::RowId::fromGlobal(current_global_row_id++),
         extractCoverageAndMutationsFromSequence<Nucleotide>(sequence, offset, REFERENCE)
            .value()
            .coverage
      );
   }

   // Bridges the old per-sequence insertion API onto the new interface: derive the
   // Coverage from the sequence and offset and hand it to insertCoverage.
   void insertNullCoverage() {
      index->insertNullSequence(column::RowId::fromGlobal(current_global_row_id++));
   }

   // Inserts a row with an explicit covered range, bypassing sequence extraction so the
   // per-batch aggregates can be checked against exactly known starts and ends.
   void insertRange(uint16_t chunk_id, uint16_t row_in_chunk, uint32_t start, uint32_t end) {
      index->insertCoverage(
         column::RowId{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk},
         Coverage{.start = start, .end = end, .missing_positions = {}}
      );
   }

   // Like insertRange, but the row also records in-region N positions, which coverage treats as
   // not covered even though they fall inside [start, end).
   void insertRangeWithMissing(
      uint16_t chunk_id,
      uint16_t row_in_chunk,
      uint32_t start,
      uint32_t end,
      std::vector<uint32_t> missing_positions
   ) {
      index->insertCoverage(
         column::RowId{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk},
         Coverage{.start = start, .end = end, .missing_positions = std::move(missing_positions)}
      );
   }

   std::unique_ptr<CoverageUnderTest> index;
   uint32_t current_global_row_id = 0;
};

/// The rows a `coveredRowsInChunk` result holds, as row-in-chunk indices.
std::vector<uint16_t> rowsIn(const roaring_util::RoaringContainer& container) {
   std::vector<uint16_t> rows;
   for (const uint16_t row : roaring_util::RoaringContainerView{container}) {
      rows.push_back(row);
   }
   return rows;
}

}  // namespace

TEST_F(HorizontalCoverageIndexChunksTest, InsertMultipleNullSequences) {
   EXPECT_NO_THROW({
      index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 0});
      index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 1});
      index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 2});
   });
}

TEST_F(HorizontalCoverageIndexChunksTest, NullSequenceIsUncoveredAtAllPositions) {
   index->insertNullSequence(
      column::RowId{.chunk_id = 0, .row_in_chunk = 0}
   );  // Sequence 0 is null

   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(50), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(99), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, MultipleNullSequencesAllUncovered) {
   index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 0});  // Sequence 0
   index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 1});  // Sequence 1
   index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 2});  // Sequence 2

   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{}));
   EXPECT_EQ(index->coveredRows(50), (roaring::Roaring{}));
}

// Coverage Insertion Tests
TEST_F(HorizontalCoverageIndexChunksTest, InsertSingleSequenceAtOffsetZero) {
   const std::string sequence = "ACGT";
   EXPECT_NO_THROW(insertSequenceCoverage(sequence, 0));
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertSequenceWithOffset) {
   const std::string sequence = "ACGT";
   EXPECT_NO_THROW(insertSequenceCoverage(sequence, 10));
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertEmptySequence) {
   const std::string sequence;
   EXPECT_NO_THROW(insertSequenceCoverage(sequence, 0));
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertMultipleSequences) {
   EXPECT_NO_THROW({
      insertSequenceCoverage("ACGT", 0);
      insertSequenceCoverage("TGCA", 5);
      insertSequenceCoverage("AAAA", 10);
   });
}

// Bitmap Retrieval Tests - Covered Positions
TEST_F(HorizontalCoverageIndexChunksTest, SequenceNotInBitmapForCoveredPositions) {
   insertSequenceCoverage("ACGT", 0);  // Covers positions 0-3

   // Sequence 0 is covered at positions 0-3, so it should appear in bitmap
   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(1), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(2), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(3), roaring::Roaring{0});
}

TEST_F(HorizontalCoverageIndexChunksTest, SequenceInBitmapForUncoveredPositions) {
   insertSequenceCoverage("ACGT", 0);  // Covers positions 0-3

   // Sequence 0 is NOT covered at positions 4+, so it should NOT appear in bitmap
   EXPECT_EQ(index->coveredRows(4), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(10), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(50), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, MultipleSequencesWithOverlap) {
   insertSequenceCoverage("ACGTACGT", 0);  // Sequence 0 covers 0-7
   insertSequenceCoverage("TGCA", 5);      // Sequence 1 covers 5-8
   insertSequenceCoverage("AAAA", 10);     // Sequence 2 covers 10-13

   // Position 6: covered by sequences 0 and 1, so only 2 is uncovered
   EXPECT_EQ(index->coveredRows(6), (roaring::Roaring{0, 1}));

   // Position 5: covered by sequences 0 and 1, so only 2 is uncovered
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{0, 1}));

   // Position 7: covered by sequences 0 and 1, so only 2 is uncovered
   EXPECT_EQ(index->coveredRows(7), (roaring::Roaring{0, 1}));

   // Position 8: covered by sequence 1 only, so 0 and 2 are uncovered
   EXPECT_EQ(index->coveredRows(8), (roaring::Roaring{1}));

   // Position 11: covered by sequence 2 only, so 0 and 1 are uncovered
   EXPECT_EQ(index->coveredRows(11), (roaring::Roaring{2}));
}

TEST_F(HorizontalCoverageIndexChunksTest, NonOverlappingSequences) {
   insertSequenceCoverage("ACGT", 0);   // Covers 0-3
   insertSequenceCoverage("TGCA", 10);  // Covers 10-13

   // Position 2: covered by 0, uncovered by 1
   EXPECT_EQ(index->coveredRows(2), roaring::Roaring{0});

   // Position 11: covered by 1, uncovered by 0
   EXPECT_EQ(index->coveredRows(11), roaring::Roaring{1});

   // Position 5: uncovered by both
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{}));
}

// N Character Tests - N counts as uncovered!
TEST_F(HorizontalCoverageIndexChunksTest, SequenceWithNIsUncoveredAtNPosition) {
   insertSequenceCoverage("ACNGT", 0);  // Position 2 has N

   // Positions 0, 1, 3, 4 are covered (A, C, G, T)
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{0}));
   EXPECT_EQ(index->coveredRows(1), (roaring::Roaring{0}));
   EXPECT_EQ(index->coveredRows(3), (roaring::Roaring{0}));
   EXPECT_EQ(index->coveredRows(4), (roaring::Roaring{0}));

   // Position 2 has N, so sequence 0 is uncovered there
   EXPECT_EQ(index->coveredRows(2), (roaring::Roaring{}));
}

TEST_F(HorizontalCoverageIndexChunksTest, SequenceWithMultipleNs) {
   insertSequenceCoverage("ANNNGTA", 0);  // Positions 1, 2, 3 have N

   // Positions with actual nucleotides are covered
   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{0});  // A
   EXPECT_EQ(index->coveredRows(4), roaring::Roaring{0});  // G
   EXPECT_EQ(index->coveredRows(5), roaring::Roaring{0});  // T
   EXPECT_EQ(index->coveredRows(6), roaring::Roaring{0});  // A

   // Positions with N are uncovered
   EXPECT_EQ(index->coveredRows(1), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(2), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(3), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, SequenceWithAllNs) {
   insertSequenceCoverage("NNNN", 5);

   // All positions with N are uncovered
   EXPECT_EQ(index->coveredRows(5), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(6), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(7), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(8), roaring::Roaring{});

   // Positions outside the sequence range are also uncovered
   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, MultipleSequencesWithNAtSamePosition) {
   insertSequenceCoverage("ACNGT", 0);  // N at position 2
   insertSequenceCoverage("AANAT", 0);  // N at position 2
   insertSequenceCoverage("ACGGT", 0);  // No N (G at position 2)

   // At position 2: sequences 0 and 1 have N (uncovered), sequence 2 has G (covered)
   EXPECT_EQ(index->coveredRows(2), (roaring::Roaring{2}));

   // At position 0: all have actual nucleotides (covered)
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{0, 1, 2}));
}

TEST_F(HorizontalCoverageIndexChunksTest, SequenceWithNAndOffset) {
   insertSequenceCoverage("ACNGT", 10);  // N at position 12

   // Position 12 has N, so sequence 0 is uncovered
   EXPECT_EQ(index->coveredRows(12), roaring::Roaring{});

   // Other positions in the sequence are covered
   EXPECT_EQ(index->coveredRows(10), roaring::Roaring{0});  // A
   EXPECT_EQ(index->coveredRows(11), roaring::Roaring{0});  // C
   EXPECT_EQ(index->coveredRows(13), roaring::Roaring{0});  // G
   EXPECT_EQ(index->coveredRows(14), roaring::Roaring{0});  // T
}

TEST_F(HorizontalCoverageIndexChunksTest, SequenceWithMixedCase) {
   insertSequenceCoverage("AcGt", 0);
   EXPECT_NO_THROW(insertSequenceCoverage("AcGt", 0));
}

TEST_F(HorizontalCoverageIndexChunksTest, SequenceWithLowercaseN) {
   insertSequenceCoverage("ACnGT", 0);  // lowercase n at position 2

   // Lowercase n should also count as uncovered
   EXPECT_EQ(index->coveredRows(2), roaring::Roaring{});
}

// Edge Cases
TEST_F(HorizontalCoverageIndexChunksTest, EmptyIndexAllPositionsEmpty) {
   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(50), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, GetBitmapAtGenomeBoundary) {
   insertSequenceCoverage("ACGT", 0);

   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 1), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, GetBitmapBeyondGenomeLength) {
   insertSequenceCoverage("ACGT", 0);

   EXPECT_EQ(index->coveredRows(GENOME_LENGTH), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH + 100), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertSequenceAtEndOfGenome) {
   insertSequenceCoverage("ACG", GENOME_LENGTH - 3);

   // Sequence covers last 3 positions
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 3), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 2), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 1), roaring::Roaring{0});

   // But not positions before
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 4), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertSequenceWithNAtEndOfGenome) {
   insertSequenceCoverage("ACN", GENOME_LENGTH - 3);

   // Positions with A and C are covered
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 3), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 2), roaring::Roaring{0});

   // Position with N is uncovered
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH - 1), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertSequenceExtendingBeyondGenome) {
   // Sequence extends beyond genome length
   EXPECT_NO_THROW(insertSequenceCoverage("ACGTACGT", GENOME_LENGTH - 2));
}

// Mixed Null and Non-Null Sequences
TEST_F(HorizontalCoverageIndexChunksTest, MixedNullAndCoveredSequences) {
   insertSequenceCoverage("ACGT", 0);  // Sequence 0: covers 0-3
   insertNullCoverage();               // Sequence 1: null (uncovered everywhere)
   insertSequenceCoverage("TGCA", 0);  // Sequence 2: covers 0-3

   // At positions 0-3: sequences 0 and 2 are covered, sequence 1 is uncovered
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{0, 2}));
   EXPECT_EQ(index->coveredRows(1), (roaring::Roaring{0, 2}));
   EXPECT_EQ(index->coveredRows(2), (roaring::Roaring{0, 2}));
   EXPECT_EQ(index->coveredRows(3), (roaring::Roaring{0, 2}));

   // At position 10: all are uncovered
   EXPECT_EQ(index->coveredRows(10), (roaring::Roaring{}));
}

TEST_F(HorizontalCoverageIndexChunksTest, MultipleNullsWithCoverage) {
   insertNullCoverage();               // Sequence 0: null
   insertNullCoverage();               // Sequence 1: null
   insertSequenceCoverage("ACGT", 5);  // Sequence 2: covers 5-8
   insertNullCoverage();               // Sequence 3: null

   // At position 0: all nulls are uncovered, seq 2 is also uncovered
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{}));

   // At positions 5-8: nulls are uncovered, seq 2 is covered
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{2}));
   EXPECT_EQ(index->coveredRows(6), (roaring::Roaring{2}));
}

TEST_F(HorizontalCoverageIndexChunksTest, MixedNullCoverageAndNCharacters) {
   insertSequenceCoverage("ACNGT", 0);  // Sequence 0: N at position 2
   insertNullCoverage();                // Sequence 1: null everywhere
   insertSequenceCoverage("ACGGT", 0);  // Sequence 2: fully covered 0-4

   // Position 2: seq 0 has N (uncovered), seq 1 is null (uncovered), seq 2 has G (covered)
   EXPECT_EQ(index->coveredRows(2), (roaring::Roaring{2}));

   // Position 0: seq 0 has A (covered), seq 1 is null (uncovered), seq 2 has A (covered)
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{0, 2}));
}

// Overlapping Coverage Tests
TEST_F(HorizontalCoverageIndexChunksTest, CompletelyOverlappingSequences) {
   insertSequenceCoverage("AAAA", 5);
   insertSequenceCoverage("TTTT", 5);
   insertSequenceCoverage("GGGG", 5);

   // At positions 5-8: all three sequences are covered
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{0, 1, 2}));
   EXPECT_EQ(index->coveredRows(6), (roaring::Roaring{0, 1, 2}));
   EXPECT_EQ(index->coveredRows(7), (roaring::Roaring{0, 1, 2}));
   EXPECT_EQ(index->coveredRows(8), (roaring::Roaring{0, 1, 2}));

   // At position 4: all three sequences are uncovered
   EXPECT_EQ(index->coveredRows(4), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, OverlappingSequencesWithN) {
   insertSequenceCoverage("ANNN", 5);  // Only position 5 covered
   insertSequenceCoverage("NANN", 5);  // Only position 6 covered
   insertSequenceCoverage("NNAN", 5);  // Only position 7 covered

   // Position 5: seq 0 covered, seq 1 and 2 have N
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{0}));

   // Position 6: seq 1 covered, seq 0 and 2 have N
   EXPECT_EQ(index->coveredRows(6), (roaring::Roaring{1}));

   // Position 7: seq 2 covered, seq 0 and 1 have N
   EXPECT_EQ(index->coveredRows(7), (roaring::Roaring{2}));

   // Position 8: all have N
   EXPECT_EQ(index->coveredRows(8), (roaring::Roaring{}));
}

TEST_F(HorizontalCoverageIndexChunksTest, PartiallyOverlappingSequences) {
   insertSequenceCoverage("AAAAAAAA", 0);  // 0-7
   insertSequenceCoverage("TTTTTTTT", 4);  // 4-11
   insertSequenceCoverage("GGGGGGGG", 8);  // 8-15

   // Position 2: only seq 0 is covered
   EXPECT_EQ(index->coveredRows(2), (roaring::Roaring{0}));

   // Position 5: seq 0 and 1 are covered, seq 2 is uncovered
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{0, 1}));

   // Position 9: seq 1 and 2 are covered, seq 0 is uncovered
   EXPECT_EQ(index->coveredRows(9), (roaring::Roaring{1, 2}));

   // Position 14: only seq 2 is covered
   EXPECT_EQ(index->coveredRows(14), (roaring::Roaring{2}));
}

TEST_F(HorizontalCoverageIndexChunksTest, AdjacentNonOverlappingSequences) {
   insertSequenceCoverage("AAAA", 0);  // 0-3
   insertSequenceCoverage("TTTT", 4);  // 4-7
   insertSequenceCoverage("GGGG", 8);  // 8-11

   EXPECT_EQ(index->coveredRows(3), (roaring::Roaring{0}));
   EXPECT_EQ(index->coveredRows(4), (roaring::Roaring{1}));
   EXPECT_EQ(index->coveredRows(7), (roaring::Roaring{1}));
   EXPECT_EQ(index->coveredRows(8), (roaring::Roaring{2}));
}

// Large Dataset Tests
TEST_F(HorizontalCoverageIndexChunksTest, ManySequences) {
   constexpr size_t NUM_SEQUENCES = 1000;

   for (size_t i = 0; i < NUM_SEQUENCES; ++i) {
      insertSequenceCoverage("ACGT", 0);
   }
   roaring::Roaring full_bitmap;
   full_bitmap.addRange(0, NUM_SEQUENCES);

   // All sequences cover position 0, so bitmap should be full
   EXPECT_EQ(index->coveredRows(0), full_bitmap);

   // All sequences are uncovered at position 10
   EXPECT_EQ(index->coveredRows(10), roaring::Roaring{});
}

TEST_F(HorizontalCoverageIndexChunksTest, ManySequencesAtDifferentPositions) {
   constexpr size_t NUM_SEQUENCES = 100;

   for (size_t i = 0; i < NUM_SEQUENCES; ++i) {
      insertSequenceCoverage("A", i);  // Each sequence covers one position
   }

   for (uint32_t pos = 0; pos < NUM_SEQUENCES; ++pos) {
      // At each position, only one sequence is covered, all others are uncovered
      EXPECT_EQ(index->coveredRows(pos), roaring::Roaring{pos});
   }
}

TEST_F(HorizontalCoverageIndexChunksTest, LongSequence) {
   const std::string long_sequence(GENOME_LENGTH / 2, 'A');
   insertSequenceCoverage(long_sequence, 0);

   // Verify coverage
   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH / 4), roaring::Roaring{0});
   EXPECT_EQ(index->coveredRows((GENOME_LENGTH / 2) - 1), roaring::Roaring{0});

   // Verify uncovered
   EXPECT_EQ(index->coveredRows(GENOME_LENGTH / 2), roaring::Roaring{});
}

// Sequence Order Tests
TEST_F(HorizontalCoverageIndexChunksTest, InsertSequencesInOrder) {
   insertSequenceCoverage("A", 0);
   insertSequenceCoverage("T", 0);
   insertSequenceCoverage("G", 0);

   // All covered at position 0
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{0, 1, 2}));

   // All uncovered at position 5
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{}));
}

TEST_F(HorizontalCoverageIndexChunksTest, InsertSequencesOutOfOrder) {
   insertSequenceCoverage("G", 0);
   insertSequenceCoverage("A", 0);
   insertSequenceCoverage("T", 0);

   // All covered at position 0
   EXPECT_EQ(index->coveredRows(0), (roaring::Roaring{0, 1, 2}));

   // All uncovered at position 5
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{}));
}

// Empty Sequence Tests
TEST_F(HorizontalCoverageIndexChunksTest, EmptySequenceIsAlwaysUncovered) {
   insertSequenceCoverage("", 5);

   // Empty sequence doesn't cover anything, so it's uncovered everywhere
   EXPECT_EQ(index->coveredRows(0), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(5), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(10), roaring::Roaring{});
}

// computeCoverageCardinalities must return, for every position, the cardinality of the bitmap
// that coveredRows produces there - that equivalence is what lets finalize skip
// materializing the bitmaps.
TEST_F(HorizontalCoverageIndexChunksTest, CoverageCardinalitiesMatchBitmapCardinalities) {
   // A mix of ranges, an internal N run (populating the horizontal bitmaps), leading/trailing N,
   // a fully missing sequence and an empty one, so every branch contributes.
   insertSequenceCoverage(std::string(GENOME_LENGTH, 'A'), 0);
   insertSequenceCoverage("ACGTACGT", 10);
   insertSequenceCoverage("ACGTNNNNNNACGT", 20);  // internal N run
   insertSequenceCoverage("NNNACGTNNN", 40);      // leading and trailing N
   insertSequenceCoverage(std::string(15, 'N'), 5);
   insertNullCoverage();
   insertSequenceCoverage("", 30);

   const std::vector<uint64_t> cardinalities = index->computeCoverageCardinalities(GENOME_LENGTH);

   ASSERT_EQ(cardinalities.size(), GENOME_LENGTH);
   for (uint32_t position_idx = 0; position_idx < GENOME_LENGTH; ++position_idx) {
      EXPECT_EQ(cardinalities.at(position_idx), index->coveredRows(position_idx).cardinality())
         << "at position " << position_idx;
   }
}

TEST_F(HorizontalCoverageIndexChunksTest, CoverageCardinalitiesAcrossMultipleChunks) {
   // Spread rows across three chunks so the per-chunk iteration in computeCoverageCardinalities is
   // exercised, with a gap chunk boundary that carries N runs.
   for (size_t chunk_id = 0; chunk_id < 3; ++chunk_id) {
      index->insertCoverage(
         column::RowId{.chunk_id = static_cast<uint16_t>(chunk_id), .row_in_chunk = 0},
         extractCoverageAndMutationsFromSequence<Nucleotide>("ACGTNNACGT", chunk_id * 5, REFERENCE)
            .value()
            .coverage
      );
   }

   const std::vector<uint64_t> cardinalities = index->computeCoverageCardinalities(GENOME_LENGTH);

   ASSERT_EQ(cardinalities.size(), GENOME_LENGTH);
   for (uint32_t position_idx = 0; position_idx < GENOME_LENGTH; ++position_idx) {
      EXPECT_EQ(cardinalities.at(position_idx), index->coveredRows(position_idx).cardinality())
         << "at position " << position_idx;
   }
}

// The four per-batch aggregates are maintained incrementally in insertCoverage and are what the
// chunk-skipping in coveredRowsInChunk relies on, so each one is pinned separately.
// The ranges below are chosen so that all four aggregates differ within every chunk - a swap
// between any two of them changes the expected values.
TEST_F(HorizontalCoverageIndexChunksTest, PerBatchAggregatesAcrossChunks) {
   // Chunk 0: starts {10, 5, 30}, ends {40, 20, 35}
   insertRange(0, 0, 10, 40);
   insertRange(0, 1, 5, 20);
   insertRange(0, 2, 30, 35);
   // Chunk 1: starts {50, 70}, ends {60, 90}
   insertRange(1, 0, 50, 60);
   insertRange(1, 1, 70, 90);
   // Chunk 2: a null row alongside a covered one, so the null's (0, 0) range sets both minima
   index->insertNullSequence(column::RowId{.chunk_id = 2, .row_in_chunk = 0});
   insertRange(2, 1, 60, 70);

   ASSERT_EQ(index->numChunks(), 3);

   EXPECT_EQ(index->batchMinStart(), (std::vector<uint32_t>{5, 50, 0}));
   EXPECT_EQ(index->batchMaxStart(), (std::vector<uint32_t>{30, 70, 60}));
   EXPECT_EQ(index->batchMinEnd(), (std::vector<uint32_t>{20, 60, 0}));
   EXPECT_EQ(index->batchMaxEnd(), (std::vector<uint32_t>{40, 90, 70}));
}

TEST_F(HorizontalCoverageIndexChunksTest, PerBatchAggregatesOfSingleRowChunkAreThatRowsRange) {
   insertRange(0, 0, 13, 27);

   EXPECT_EQ(index->batchMinStart(), (std::vector<uint32_t>{13}));
   EXPECT_EQ(index->batchMaxStart(), (std::vector<uint32_t>{13}));
   EXPECT_EQ(index->batchMinEnd(), (std::vector<uint32_t>{27}));
   EXPECT_EQ(index->batchMaxEnd(), (std::vector<uint32_t>{27}));
}

// Sequence-derived rows must produce the same aggregates as the explicit ranges above, since that
// is the only path preprocessing takes.
TEST_F(HorizontalCoverageIndexChunksTest, PerBatchAggregatesFromSequenceCoverage) {
   insertSequenceCoverage("ACGT", 10);       // covers [10, 14)
   insertSequenceCoverage("ACGTACGTAC", 4);  // covers [4, 14)
   insertSequenceCoverage("NNACGTNN", 20);   // leading and trailing N still cover [22, 26)
   insertNullCoverage();                     // covers [0, 0)

   EXPECT_EQ(index->batchMinStart(), (std::vector<uint32_t>{0}));
   EXPECT_EQ(index->batchMaxStart(), (std::vector<uint32_t>{22}));
   EXPECT_EQ(index->batchMinEnd(), (std::vector<uint32_t>{0}));
   EXPECT_EQ(index->batchMaxEnd(), (std::vector<uint32_t>{26}));
}

// ---------------------------------------------------------------------------
// coverageScan: the scalar and AVX-512 implementations must agree
// ---------------------------------------------------------------------------

namespace {

constexpr size_t BITSET_WORDS = 1024;  // 2^16 bits, one per row of a chunk

struct ScanInput {
   std::vector<uint32_t> starts;
   std::vector<uint32_t> ends;
};

/// Row ranges covering the cases the kernels have to get right: ordinary ranges, empty ones
/// (`start == end`), inverted ones (`start > end`), ranges touching 0 and the end of the genome,
/// and full-width ranges.
ScanInput randomScanInput(size_t num_rows, uint32_t genome_length, std::mt19937& random) {
   std::uniform_int_distribution<uint32_t> position_distribution(0, genome_length);
   std::uniform_int_distribution<int> shape_distribution(0, 9);
   ScanInput input;
   input.starts.reserve(num_rows);
   input.ends.reserve(num_rows);
   for (size_t row = 0; row < num_rows; ++row) {
      uint32_t start = position_distribution(random);
      uint32_t end = position_distribution(random);
      switch (shape_distribution(random)) {
         case 0:
            start = 0;
            end = genome_length;
            break;
         case 1:
            end = start;  // empty
            break;
         case 2:
            std::swap(start, end);  // possibly inverted
            break;
         case 3:
            start = 0;
            break;
         case 4:
            end = genome_length;
            break;
         default:
            if (start > end) {
               std::swap(start, end);
            }
      }
      input.starts.push_back(start);
      input.ends.push_back(end);
   }
   return input;
}

/// The specification, written out directly: bit `row` iff `starts[row] <= position < ends[row]`.
std::pair<std::vector<uint64_t>, uint32_t> referenceScan(
   const ScanInput& input,
   size_t num_rows,
   uint32_t position
) {
   std::vector<uint64_t> bitset(BITSET_WORDS, 0);
   uint32_t cardinality = 0;
   for (size_t row = 0; row < num_rows; ++row) {
      if (input.starts[row] <= position && position < input.ends[row]) {
         bitset[row / 64] |= uint64_t{1} << (row % 64);
         ++cardinality;
      }
   }
   return {std::move(bitset), cardinality};
}

// Row counts around the AVX-512 kernel's 16-row stride and the bitset's 64-row word: the tail loop
// and each of the four in-word bit offsets (0/16/32/48) have to be hit.
const std::vector<size_t> ROW_COUNTS =
   {0, 1, 15, 16, 17, 31, 32, 33, 48, 63, 64, 65, 79, 128, 1000, 4096};

}  // namespace

// Runs everywhere, and pins down the bit packing (`row / 64`, `row % 64`) that the AVX-512 kernel
// then has to reproduce with a shifted 16-bit mask.
TEST(HorizontalCoverageIndexScan, scalarMatchesSpecification) {
   // A fixed seed keeps the test reproducible.
   // NOLINTNEXTLINE(bugprone-random-generator-seed)
   std::mt19937 random{20260922};
   constexpr uint32_t GENOME_LENGTH = 512;

   for (const size_t num_rows : ROW_COUNTS) {
      const ScanInput input = randomScanInput(num_rows, GENOME_LENGTH, random);
      for (const uint32_t position :
           {0U, 1U, GENOME_LENGTH / 2, GENOME_LENGTH - 1, GENOME_LENGTH}) {
         auto [expected_bitset, expected_cardinality] = referenceScan(input, num_rows, position);

         std::vector<uint64_t> bitset(BITSET_WORDS, 0);
         const uint32_t cardinality = detail::coverageScanScalar(
            input.starts.data(), input.ends.data(), num_rows, position, bitset.data()
         );

         EXPECT_EQ(cardinality, expected_cardinality)
            << "num_rows=" << num_rows << " position=" << position;
         EXPECT_EQ(bitset, expected_bitset) << "num_rows=" << num_rows << " position=" << position;
      }
   }
}

// The AVX-512 kernel can only be run where the CPU supports it, so on a dev machine without
// AVX-512 (e.g. Apple Silicon) this reports as skipped and asserts nothing. It is the only thing
// that checks that kernel, so it is worth making sure it actually runs somewhere: the skip message
// names the reason, and `RHYDB_HAS_X86_SIMD` tells you whether the build even contains the kernel.
TEST(HorizontalCoverageIndexScan, avx512MatchesScalar) {
#ifndef RHYDB_HAS_X86_SIMD
   GTEST_SKIP() << "this build has no x86 SIMD path, so there is no AVX-512 kernel to compare";
#else
   if (!detail::cpuHasAvx512()) {
      GTEST_SKIP() << "CPU lacks avx512f/avx512bw, so coverageScanAvx512 cannot be executed here";
   }

   // A fixed seed keeps the test reproducible.
   // NOLINTNEXTLINE(bugprone-random-generator-seed)
   std::mt19937 random{20260922};
   constexpr uint32_t GENOME_LENGTH = 512;

   for (const size_t num_rows : ROW_COUNTS) {
      const ScanInput input = randomScanInput(num_rows, GENOME_LENGTH, random);
      // Sweep every position, not a handful: a mask or shift bug may only show up for particular
      // row/position combinations, and this is cheap.
      for (uint32_t position = 0; position <= GENOME_LENGTH; ++position) {
         std::vector<uint64_t> scalar_bitset(BITSET_WORDS, 0);
         const uint32_t scalar_cardinality = detail::coverageScanScalar(
            input.starts.data(), input.ends.data(), num_rows, position, scalar_bitset.data()
         );

         std::vector<uint64_t> avx512_bitset(BITSET_WORDS, 0);
         const uint32_t avx512_cardinality = detail::coverageScanAvx512(
            input.starts.data(), input.ends.data(), num_rows, position, avx512_bitset.data()
         );

         ASSERT_EQ(avx512_cardinality, scalar_cardinality)
            << "num_rows=" << num_rows << " position=" << position;
         ASSERT_EQ(avx512_bitset, scalar_bitset)
            << "num_rows=" << num_rows << " position=" << position;
      }
   }
#endif
}

// ---------------------------------------------------------------------------
// coveredRowsInChunk / noRowCoversPositionInChunk / positionCoveredByWholeChunk
//
// These are only reached indirectly through query tests, where a bug shows up as an opaque wrong
// count. The cases below pin each branch down directly: the two O(1) envelope fast paths, the
// scan, in-region-N removal, and the empty results (which take two different routes).
// ---------------------------------------------------------------------------

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkOutOfRangeChunkIdIsEmpty) {
   insertRange(0, 0, 0, 50);

   EXPECT_TRUE(rowsIn(index->coveredRowsInChunk(10, 1)).empty());
   EXPECT_TRUE(rowsIn(index->coveredRowsInChunk(10, 999)).empty());
   // Nothing can cover a position in a chunk that does not exist ...
   EXPECT_TRUE(index->noRowCoversPositionInChunk(10, 1));
   // ... and "every row covers it" is false rather than vacuously true.
   EXPECT_FALSE(index->positionCoveredByWholeChunk(10, 1));
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkWholeChunkFastPath) {
   constexpr uint16_t ROWS = 20;
   for (uint16_t row = 0; row < ROWS; ++row) {
      insertRange(0, row, 10, 20);
   }

   EXPECT_TRUE(index->positionCoveredByWholeChunk(15, 0));
   EXPECT_FALSE(index->noRowCoversPositionInChunk(15, 0));

   std::vector<uint16_t> expected(ROWS);
   // NOLINTNEXTLINE(modernize-use-ranges)
   std::iota(expected.begin(), expected.end(), uint16_t{0});
   EXPECT_EQ(rowsIn(index->coveredRowsInChunk(15, 0)), expected);

   // The envelope is half-open: the start is covered, the end is not.
   EXPECT_TRUE(index->positionCoveredByWholeChunk(10, 0));
   EXPECT_FALSE(index->positionCoveredByWholeChunk(20, 0));
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkPartialCoverageAcrossSimdStride) {
   // 100 rows so the scan runs its 16-row stride several times and still has a 4-row tail.
   constexpr uint16_t ROWS = 100;
   for (uint16_t row = 0; row < ROWS; ++row) {
      const bool even = row % 2 == 0;
      insertRange(0, row, even ? 0 : 40, even ? 30 : 60);
   }

   // Neither fast path applies: the position is inside the covered envelope but outside the
   // intersection envelope, so this goes through the scan.
   EXPECT_FALSE(index->noRowCoversPositionInChunk(15, 0));
   EXPECT_FALSE(index->positionCoveredByWholeChunk(15, 0));

   std::vector<uint16_t> expected;
   for (uint16_t row = 0; row < ROWS; row += 2) {
      expected.push_back(row);
   }
   EXPECT_EQ(rowsIn(index->coveredRowsInChunk(15, 0)), expected);
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkExcludesRowsWithInRegionN) {
   for (uint16_t row = 0; row < 5; ++row) {
      if (row == 2) {
         insertRangeWithMissing(0, row, 0, 50, {20});
      } else {
         insertRange(0, row, 0, 50);
      }
   }

   // Row 2 carries an N at 20, so it is missing there rather than covered -- even though 20 lies
   // inside its [0, 50) range and every row's range covers it.
   EXPECT_EQ(rowsIn(index->coveredRowsInChunk(20, 0)), (std::vector<uint16_t>{0, 1, 3, 4}));
   EXPECT_FALSE(index->positionCoveredByWholeChunk(20, 0));

   // One position over, nothing is missing and the whole-chunk fast path applies again.
   EXPECT_TRUE(index->positionCoveredByWholeChunk(21, 0));
   EXPECT_EQ(rowsIn(index->coveredRowsInChunk(21, 0)), (std::vector<uint16_t>{0, 1, 2, 3, 4}));
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkEmptyWhenPositionOutsideEnvelope) {
   for (uint16_t row = 0; row < 8; ++row) {
      insertRange(0, row, 10, 20);
   }

   // Past every row's end, and before every row's start: both are the O(1) rejection.
   EXPECT_TRUE(index->noRowCoversPositionInChunk(20, 0));
   EXPECT_TRUE(index->noRowCoversPositionInChunk(5, 0));
   EXPECT_TRUE(rowsIn(index->coveredRowsInChunk(20, 0)).empty());
   EXPECT_TRUE(rowsIn(index->coveredRowsInChunk(5, 0)).empty());
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkEmptyWhenEnvelopeAdmitsButNoRowCovers) {
   // A gap between the two rows' ranges: 15 is inside the chunk's covered envelope [0, 30), so the
   // O(1) rejection does not fire, but the scan still finds nothing. This is the other way a result
   // comes back empty, and the one that has to free the bitset it speculatively allocated.
   insertRange(0, 0, 0, 10);
   insertRange(0, 1, 20, 30);

   EXPECT_FALSE(index->noRowCoversPositionInChunk(15, 0));
   EXPECT_FALSE(index->positionCoveredByWholeChunk(15, 0));
   EXPECT_TRUE(rowsIn(index->coveredRowsInChunk(15, 0)).empty());
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsInChunkReturnsRowInChunkIndices) {
   // Chunks must be filled in order, so chunk 0 exists before chunk 1.
   for (uint16_t row = 0; row < 3; ++row) {
      insertRange(0, row, 0, 5);
   }
   for (uint16_t row = 0; row < 4; ++row) {
      insertRange(1, row, 0, 50);
   }

   // The result is one container for this chunk alone, so it holds row-in-chunk indices, not the
   // global row ids (which for chunk 1 start at 65536).
   EXPECT_EQ(rowsIn(index->coveredRowsInChunk(10, 1)), (std::vector<uint16_t>{0, 1, 2, 3}));
   // Chunk 0's rows end at 5, so the same position matches nothing there.
   EXPECT_TRUE(rowsIn(index->coveredRowsInChunk(10, 0)).empty());
}

TEST_F(HorizontalCoverageIndexChunksTest, PositionCoveredByWholeChunkIsFalseWithANullRow) {
   insertRange(0, 0, 0, 50);
   insertRange(0, 1, 0, 50);
   index->insertNullSequence(column::RowId{.chunk_id = 0, .row_in_chunk = 2});

   // A null row has range [0, 0), which drags the intersection envelope's end to 0, so no position
   // is covered by the whole chunk.
   EXPECT_FALSE(index->positionCoveredByWholeChunk(10, 0));
   // The other two rows still cover it.
   EXPECT_EQ(rowsIn(index->coveredRowsInChunk(10, 0)), (std::vector<uint16_t>{0, 1}));
}

TEST_F(HorizontalCoverageIndexChunksTest, CoveredRowsAcrossChunks) {
   // Chunk 0: two rows cover the same range, one with in-region Ns at 10..12.
   insertRange(0, 0, 0, 40);
   insertRangeWithMissing(0, 1, 0, 40, {10, 11, 12});
   // Chunk 1: overlapping ranges, an in-region N and a null row.
   insertRange(1, 0, 20, 60);
   insertRangeWithMissing(1, 1, 30, 80, {35});
   index->insertNullSequence(column::RowId{.chunk_id = 1, .row_in_chunk = 2});
   // Chunk 2: a single row near the end of the genome, with an in-region N.
   insertRangeWithMissing(2, 0, 90, GENOME_LENGTH, {95});

   const uint32_t chunk_1 = 1U << 16U;
   const uint32_t chunk_2 = 2U << 16U;
   EXPECT_EQ(index->coveredRows(5), (roaring::Roaring{0, 1}));
   EXPECT_EQ(index->coveredRows(11), (roaring::Roaring{0}));
   EXPECT_EQ(index->coveredRows(25), (roaring::Roaring{0, 1, chunk_1}));
   EXPECT_EQ(index->coveredRows(35), (roaring::Roaring{0, 1, chunk_1}));
   EXPECT_EQ(index->coveredRows(45), (roaring::Roaring{chunk_1, chunk_1 + 1}));
   EXPECT_EQ(index->coveredRows(70), (roaring::Roaring{chunk_1 + 1}));
   EXPECT_EQ(index->coveredRows(85), roaring::Roaring{});
   EXPECT_EQ(index->coveredRows(92), (roaring::Roaring{chunk_2}));
   EXPECT_EQ(index->coveredRows(95), roaring::Roaring{});
}

}  // namespace rhydb::storage::sequence

// ---------------------------------------------------------------------------
// The per-chunk blob: missing ranges, re-reading the bytes, validation and serialization
// ---------------------------------------------------------------------------

namespace rhydb::storage::sequence {

namespace {

common::Buffer exampleBuffer() {
   HorizontalCoverageIndexBuilder builder;
   // Unsorted, duplicated and partly out-of-range missing positions collapse into sorted ranges
   // inside [10, 40).
   const std::vector<uint32_t> missing{22, 20, 21, 5, 30, 21, 39, 45};
   builder.addRow(10, 40, std::span<const uint32_t>{missing});
   builder.addNullRow();
   builder.addRow(0, 50, std::span<const uint32_t>{});
   return std::move(builder).finish();
}

/// A copy of `bytes` in freshly allocated memory, as it would be after reading it back from disk.
common::Buffer copyOf(std::span<const std::byte> bytes) {
   common::BufferBuilder builder{bytes.size()};
   builder.write(0, bytes);
   return std::move(builder).finish();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void expectExampleContent(const HorizontalCoverageIndex& index) {
   ASSERT_EQ(index.numRows(), 3);
   EXPECT_EQ(index.coverageRange(0), (std::pair<uint32_t, uint32_t>{10, 40}));
   EXPECT_EQ(index.coverageRange(1), (std::pair<uint32_t, uint32_t>{0, 0}));
   EXPECT_EQ(index.coverageRange(2), (std::pair<uint32_t, uint32_t>{0, 50}));

   const auto ranges = index.missingRanges(0);
   ASSERT_EQ(ranges.size(), 3);
   EXPECT_EQ(ranges[0].begin, 20);
   EXPECT_EQ(ranges[0].end, 23);
   EXPECT_EQ(ranges[1].begin, 30);
   EXPECT_EQ(ranges[1].end, 31);
   EXPECT_EQ(ranges[2].begin, 39);
   EXPECT_EQ(ranges[2].end, 40);
   EXPECT_TRUE(index.missingRanges(1).empty());
   EXPECT_TRUE(index.missingRanges(2).empty());

   EXPECT_TRUE(index.covers(0, 19));
   EXPECT_FALSE(index.covers(0, 20));
   EXPECT_FALSE(index.covers(0, 22));
   EXPECT_TRUE(index.covers(0, 23));
   EXPECT_FALSE(index.covers(0, 39));
   EXPECT_FALSE(index.covers(1, 0));
   EXPECT_TRUE(index.covers(2, 49));

   EXPECT_EQ(index.minStart(), 0);
   EXPECT_EQ(index.maxStart(), 10);
   EXPECT_EQ(index.minEnd(), 0);
   EXPECT_EQ(index.maxEnd(), 50);
}

std::vector<std::byte> bytesOf(const common::Buffer& buffer) {
   return {buffer.bytes().begin(), buffer.bytes().end()};
}

HorizontalCoverageIndexHeader headerOf(const std::vector<std::byte>& bytes) {
   HorizontalCoverageIndexHeader header;
   std::memcpy(&header, bytes.data(), sizeof(header));
   return header;
}

void rereadBytes(const std::vector<std::byte>& bytes) {
   const auto copy = copyOf(bytes);
   static_cast<void>(HorizontalCoverageIndex::fromBuffer(copy));
}

}  // namespace

TEST(HorizontalCoverageIndexBlob, collapsesMissingPositionsIntoRangesInsideTheCoveredRange) {
   const auto buffer = exampleBuffer();
   expectExampleContent(HorizontalCoverageIndex::fromBuffer(buffer));
}

TEST(HorizontalCoverageIndexBlob, emptyIndexHasNoRowsAndAnEmptyEnvelope) {
   const auto buffer = HorizontalCoverageIndexBuilder{}.finish();
   const auto index = HorizontalCoverageIndex::fromBuffer(buffer);
   EXPECT_EQ(index.numRows(), 0);
   EXPECT_TRUE(index.noRowCovers(0));
   EXPECT_FALSE(index.everyRowCovers(0));
   const auto copy = copyOf(buffer.bytes());
   EXPECT_EQ(HorizontalCoverageIndex::fromBuffer(copy).numRows(), 0);
}

TEST(HorizontalCoverageIndexBlob, rereadBytesAreReadInPlace) {
   const auto copy = copyOf(exampleBuffer().bytes());
   const auto index = HorizontalCoverageIndex::fromBuffer(copy);
   expectExampleContent(index);
   const auto* starts = reinterpret_cast<const std::byte*>(index.starts().data());  // NOLINT
   EXPECT_GE(starts, copy.bytes().data());
   EXPECT_LT(starts, copy.bytes().data() + copy.size());
}

TEST(HorizontalCoverageIndexBlob, rejectsMalformedBytes) {
   const auto original = bytesOf(exampleBuffer());
   auto expect_rejected = [&](auto corrupt, std::string_view message) {
      auto bytes = original;
      corrupt(bytes);
      EXPECT_THAT(
         [&]() { rereadBytes(bytes); },
         ::testing::ThrowsMessage<std::runtime_error>(::testing::HasSubstr(std::string{message}))
      );
   };
   auto with_header = [](auto change) {
      return [change](std::vector<std::byte>& bytes) {
         auto header = headerOf(bytes);
         change(header);
         std::memcpy(bytes.data(), &header, sizeof(header));
      };
   };

   expect_rejected(with_header([](auto& header) { header.magic = 0; }), "wrong magic number");
   expect_rejected(with_header([](auto& header) { header.num_rows = 100; }), "do not match");
   expect_rejected(
      with_header([](auto& header) { header.max_end = 49; }), "envelope does not match"
   );
   expect_rejected(
      [](std::vector<std::byte>& bytes) { bytes.resize(bytes.size() - 8); }, "do not match"
   );
   expect_rejected([](std::vector<std::byte>& bytes) { bytes.resize(10); }, "cannot hold a header");
   // Make row 0's first missing range start before its covered range.
   expect_rejected(
      [&](std::vector<std::byte>& bytes) {
         const size_t ranges_offset = bytes.size() - (3 * sizeof(MissingRange));
         const MissingRange bad{.begin = 5, .end = 6};
         std::memcpy(bytes.data() + ranges_offset, &bad, sizeof(bad));
      },
      "not sorted, disjoint and covered"
   );
}

TEST(HorizontalCoverageIndexBlob, builderTakesMissingRangesAsTheIndexReturnsThem) {
   const auto original_buffer = exampleBuffer();
   const auto original = HorizontalCoverageIndex::fromBuffer(original_buffer);
   HorizontalCoverageIndexBuilder builder;
   for (uint32_t row = 0; row < original.numRows(); ++row) {
      const auto [start, end] = original.coverageRange(static_cast<uint16_t>(row));
      builder.addRow(start, end, original.missingRanges(static_cast<uint16_t>(row)));
   }
   const auto rebuilt = std::move(builder).finish();
   expectExampleContent(HorizontalCoverageIndex::fromBuffer(rebuilt));
   EXPECT_TRUE(std::ranges::equal(rebuilt.bytes(), original_buffer.bytes()));
}

}  // namespace rhydb::storage::sequence
