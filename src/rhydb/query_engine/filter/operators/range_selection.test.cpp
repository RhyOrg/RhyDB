#include "rhydb/query_engine/filter/operators/range_selection.h"

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

using rhydb::query_engine::filter::operators::RangeSelection;
using rhydb::storage::column::RowId;
using rhydb::storage::column::RowLayout;

TEST(OperatorRangeSelection, evaluateShouldReturnCorrectValues) {
   const auto row_layout = RowLayout::of(8);
   std::vector<RangeSelection::Range> test_ranges(
      // NOLINTNEXTLINE(readability-magic-numbers)
      {{RangeSelection::Range{RowId::fromGlobal(0), RowId::fromGlobal(2)},
        RangeSelection::Range{RowId::fromGlobal(3), RowId::fromGlobal(5)},},}
   );

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 1, 3, 4}));
   auto negated = RangeSelection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({2, 5, 6, 7}));
}

TEST(OperatorRangeSelection, evaluateShouldReturnCorrectValuesEmptyDatabase) {
   std::vector<RangeSelection::Range> test_ranges;
   const auto row_layout = RowLayout::of();

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring());
   auto negated = RangeSelection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorRangeSelection, evaluateShouldReturnCorrectValuesEmptyRanges) {
   std::vector<RangeSelection::Range> test_ranges(
      {{RangeSelection::Range{RowId::fromGlobal(0), RowId::fromGlobal(0)},
        RangeSelection::Range{RowId::fromGlobal(4), RowId::fromGlobal(4)},},}
   );
   const auto row_layout = RowLayout::of(9);

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring());
   auto negated = RangeSelection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(OperatorRangeSelection, evaluateShouldReturnCorrectValuesFullRange) {
   const auto row_layout = RowLayout::of(8);
   // NOLINTNEXTLINE(readability-magic-numbers)
   std::vector<RangeSelection::Range> test_ranges({{RangeSelection::Range{
      RowId::fromGlobal(0),
      RowId{.chunk_id = static_cast<uint16_t>(row_layout.numChunks()), .row_in_chunk = 0},
   },},});

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4, 5, 6, 7}));
   auto negated = RangeSelection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorRangeSelection, evaluateShouldReturnCorrectValuesMeetingRanges) {
   std::vector<RangeSelection::Range> test_ranges(
      {{RangeSelection::Range{RowId::fromGlobal(0), RowId::fromGlobal(2)},
        RangeSelection::Range{RowId::fromGlobal(2), RowId::fromGlobal(4)},},}
   );
   const auto row_layout = RowLayout::of(9);

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);
   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));
   auto negated = RangeSelection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({4, 5, 6, 7, 8}));
}

TEST(OperatorRangeSelection, evaluateExpandsRangeSpanningMultipleChunks) {
   // Three chunks laid out at the 2^16-aligned global offsets 0, 65536 and 131072. A single range
   // from chunk 0 to chunk 2 forces the cross-chunk branch: a partial first chunk, a fully covered
   // middle chunk and a partial last chunk.
   const auto row_layout = RowLayout::of(4, 3, 5);
   std::vector<RangeSelection::Range> test_ranges({{RangeSelection::Range{
      RowId{.chunk_id = 0, .row_in_chunk = 2}, RowId{.chunk_id = 2, .row_in_chunk = 2},
   },},});

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);

   const roaring::Roaring expected({
      RowId{.chunk_id = 0, .row_in_chunk = 2}.toGlobal(),  // partial first chunk
      RowId{.chunk_id = 0, .row_in_chunk = 3}.toGlobal(),
      RowId{.chunk_id = 1, .row_in_chunk = 0}.toGlobal(),  // full middle chunk
      RowId{.chunk_id = 1, .row_in_chunk = 1}.toGlobal(),
      RowId{.chunk_id = 1, .row_in_chunk = 2}.toGlobal(),
      RowId{.chunk_id = 2, .row_in_chunk = 0}.toGlobal(),  // partial last chunk
      RowId{.chunk_id = 2, .row_in_chunk = 1}.toGlobal(),
   });
   ASSERT_EQ(under_test->evaluate().toRoaring(), expected);

   // The complement covers every other valid row id, including the cross-chunk ranges at the edges.
   auto negated = RangeSelection::negate(std::move(under_test));
   const roaring::Roaring expected_negated({
      RowId{.chunk_id = 0, .row_in_chunk = 0}.toGlobal(),
      RowId{.chunk_id = 0, .row_in_chunk = 1}.toGlobal(),
      RowId{.chunk_id = 2, .row_in_chunk = 2}.toGlobal(),
      RowId{.chunk_id = 2, .row_in_chunk = 3}.toGlobal(),
      RowId{.chunk_id = 2, .row_in_chunk = 4}.toGlobal(),
   });
   ASSERT_EQ(negated->evaluate().toRoaring(), expected_negated);
}

TEST(OperatorRangeSelection, returnsCorrectTypeInfo) {
   std::vector<RangeSelection::Range> test_ranges(
      {{RangeSelection::Range{RowId::fromGlobal(0), RowId::fromGlobal(2)},
        RangeSelection::Range{RowId::fromGlobal(2), RowId::fromGlobal(4)},},}
   );
   const auto row_layout = RowLayout::of(8);

   auto under_test = std::make_unique<RangeSelection>(std::move(test_ranges), row_layout);

   ASSERT_EQ(under_test->type(), rhydb::query_engine::filter::operators::RANGE_SELECTION);
   auto negated = RangeSelection::negate(std::move(under_test));
   ASSERT_EQ(negated->type(), rhydb::query_engine::filter::operators::RANGE_SELECTION);
}
