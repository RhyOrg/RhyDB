#include "rhydb/query_engine/filter/operators/is_in_covered_region.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/sequence/sequence_chunk.h"

using rhydb::query_engine::filter::operators::IsInCoveredRegion;
using Comparator = IsInCoveredRegion::Comparator;
using rhydb::query_engine::filter::operators::Selection;
using rhydb::storage::column::RowLayout;
using rhydb::storage::sequence::SequenceChunk;
using rhydb::storage::sequence::SequenceChunkBuilder;
using rhydb::storage::sequence::SequenceReference;

namespace {

/// A single chunk of rows covering [0, 5), except for the given missing positions
IsInCoveredRegion::Chunks chunkWithMissingPositions(
   const std::vector<std::vector<uint32_t>>& missing_positions_per_row
) {
   SequenceChunkBuilder builder{SequenceReference::create<rhydb::Nucleotide>("AAAAA")};
   for (const auto& missing_positions : missing_positions_per_row) {
      builder.addRow(
         rhydb::Coverage{.start = 0, .end = 5, .missing_positions = missing_positions}, {}
      );
   }
   return {std::make_shared<const SequenceChunk>(std::move(builder).finish())};
}

const std::vector<std::vector<uint32_t>> MISSING_POSITIONS{
   {1, 2, 3},
   {1, 3},
   {1, 2, 3},
   {},
   {3},
   {4},
   {1, 4},
   {2, 4},
};

}  // namespace

TEST(IsInCoveredRegion, containsCheckShouldReturnCorrectValues) {
   const auto chunks = chunkWithMissingPositions(MISSING_POSITIONS);
   auto under_test = std::make_unique<Selection>(
      std::make_unique<IsInCoveredRegion>(&chunks, 2, Comparator::IS_COVERED),
      RowLayout::of(chunks.at(0)->numRows())
   );
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({1, 3, 4, 5, 6}));
   auto negated = Selection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({0, 2, 7}));
}

TEST(IsInCoveredRegion, notContainsCheckShouldReturnCorrectValues) {
   const auto chunks = chunkWithMissingPositions(MISSING_POSITIONS);
   auto under_test = std::make_unique<Selection>(
      std::make_unique<IsInCoveredRegion>(&chunks, 2, Comparator::IS_NOT_COVERED),
      RowLayout::of(chunks.at(0)->numRows())
   );
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 2, 7}));
   auto negated = Selection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({1, 3, 4, 5, 6}));
}

TEST(IsInCoveredRegion, matchAgreesWithTheChunkEvaluation) {
   const auto chunks = chunkWithMissingPositions(MISSING_POSITIONS);
   const IsInCoveredRegion covered{&chunks, 2, Comparator::IS_COVERED};
   const roaring::Roaring expected{1, 3, 4, 5, 6};
   for (uint32_t row = 0; row < MISSING_POSITIONS.size(); ++row) {
      const auto row_in_chunk = static_cast<uint16_t>(row);
      EXPECT_EQ(
         covered.match(rhydb::storage::column::RowId{.chunk_id = 0, .row_in_chunk = row_in_chunk}),
         expected.contains(row)
      );
   }
}
