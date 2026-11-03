// Operators are evaluated chunk by chunk. These tests use a layout of several chunks that do not
// fill their 2^16 block, to check that results neither leak into the unused row ids between chunks
// nor lose rows at chunk boundaries.

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/intersection.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/range_selection.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/filter/operators/threshold.h"
#include "rhydb/query_engine/filter/operators/union.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/row_layout.h"

using rhydb::Bitmap;
using rhydb::query_engine::filter::operators::Complement;
using rhydb::query_engine::filter::operators::Full;
using rhydb::query_engine::filter::operators::IndexScan;
using rhydb::query_engine::filter::operators::Intersection;
using rhydb::query_engine::filter::operators::Operator;
using rhydb::query_engine::filter::operators::OperatorVector;
using rhydb::query_engine::filter::operators::Predicate;
using rhydb::query_engine::filter::operators::RangeSelection;
using rhydb::query_engine::filter::operators::Selection;
using rhydb::query_engine::filter::operators::Threshold;
using rhydb::query_engine::filter::operators::Union;
using rhydb::storage::column::RowId;
using rhydb::storage::column::RowLayout;

namespace {

// Chunk 0 holds 5 rows, chunk 1 holds 3 rows and chunk 2 holds 4 rows
const RowLayout LAYOUT = RowLayout::of(5, 3, 4);

uint32_t row(uint16_t chunk_id, uint16_t row_in_chunk) {
   return RowId{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk}.toGlobal();
}

roaring::Roaring rows(std::initializer_list<uint32_t> global_row_ids) {
   return roaring::Roaring{global_row_ids};
}

// Owns the bitmap an IndexScan views
class TestIndex {
   roaring::Roaring bitmap;

  public:
   explicit TestIndex(roaring::Roaring bitmap)
       : bitmap(std::move(bitmap)) {}

   [[nodiscard]] std::unique_ptr<IndexScan> scan() const {
      return std::make_unique<IndexScan>(Bitmap{&bitmap}, LAYOUT);
   }
};

// Matches the rows with an even position in their chunk
class EvenRowInChunk : public Predicate {
  public:
   [[nodiscard]] std::string toString() const override { return "EvenRowInChunk"; }

   [[nodiscard]] bool match(RowId row_id) const override { return row_id.row_in_chunk % 2 == 0; }

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override {
      return std::make_unique<EvenRowInChunk>();
   }

   [[nodiscard]] std::unique_ptr<Predicate> negate() const override {
      throw std::logic_error("not needed in the tests");
   }
};

}  // namespace

TEST(ChunkedEvaluation, fullSelectsOnlyTheRowsOfEachChunk) {
   const Full under_test(LAYOUT);
   EXPECT_EQ(
      under_test.evaluate().toRoaring(),
      rows(
         {row(0, 0),
          row(0, 1),
          row(0, 2),
          row(0, 3),
          row(0, 4),
          row(1, 0),
          row(1, 1),
          row(1, 2),
          row(2, 0),
          row(2, 1),
          row(2, 2),
          row(2, 3)}
      )
   );
}

TEST(ChunkedEvaluation, complementStaysWithinEachChunk) {
   const TestIndex index{rows({row(0, 1), row(0, 2), row(0, 3), row(0, 4), row(1, 1), row(2, 0)})};
   const Complement under_test(index.scan(), LAYOUT);
   EXPECT_EQ(
      under_test.evaluate().toRoaring(),
      rows({row(0, 0), row(1, 0), row(1, 2), row(2, 1), row(2, 2), row(2, 3)})
   );
}

TEST(ChunkedEvaluation, rangeSpanningChunksSkipsTheUnusedRowIdsBetweenThem) {
   std::vector<RangeSelection::Range> ranges;
   ranges.emplace_back(
      RowId{.chunk_id = 0, .row_in_chunk = 3}, RowId{.chunk_id = 2, .row_in_chunk = 1}
   );
   const RangeSelection under_test(std::move(ranges), LAYOUT);
   EXPECT_EQ(
      under_test.evaluate().toRoaring(),
      rows({row(0, 3), row(0, 4), row(1, 0), row(1, 1), row(1, 2), row(2, 0)})
   );
}

TEST(ChunkedEvaluation, negatedRangeSpanningChunks) {
   std::vector<RangeSelection::Range> ranges;
   ranges.emplace_back(
      RowId{.chunk_id = 0, .row_in_chunk = 3}, RowId{.chunk_id = 2, .row_in_chunk = 1}
   );
   auto under_test = Operator::negate(std::make_unique<RangeSelection>(std::move(ranges), LAYOUT));
   EXPECT_EQ(
      under_test->evaluate().toRoaring(),
      rows({row(0, 0), row(0, 1), row(0, 2), row(2, 1), row(2, 2), row(2, 3)})
   );
}

TEST(ChunkedEvaluation, unionAndIntersectionCombineEachChunkSeparately) {
   const TestIndex first{rows({row(0, 0), row(0, 1), row(2, 3)})};
   const TestIndex second{rows({row(0, 1), row(1, 2), row(2, 3)})};

   OperatorVector union_children;
   union_children.push_back(first.scan());
   union_children.push_back(second.scan());
   const Union union_under_test(std::move(union_children), LAYOUT);
   EXPECT_EQ(
      union_under_test.evaluate().toRoaring(), rows({row(0, 0), row(0, 1), row(1, 2), row(2, 3)})
   );

   OperatorVector intersection_children;
   intersection_children.push_back(first.scan());
   intersection_children.push_back(second.scan());
   const Intersection intersection_under_test(std::move(intersection_children), {}, LAYOUT);
   EXPECT_EQ(intersection_under_test.evaluate().toRoaring(), rows({row(0, 1), row(2, 3)}));
}

TEST(ChunkedEvaluation, intersectionWithNegatedChildren) {
   const TestIndex children{rows({row(0, 0), row(0, 1), row(1, 0), row(1, 1), row(2, 2)})};
   const TestIndex negated{rows({row(0, 1), row(1, 0), row(1, 2)})};

   OperatorVector non_negated_children;
   non_negated_children.push_back(children.scan());
   OperatorVector negated_children;
   negated_children.push_back(negated.scan());
   const Intersection under_test(
      std::move(non_negated_children), std::move(negated_children), LAYOUT
   );
   EXPECT_EQ(under_test.evaluate().toRoaring(), rows({row(0, 0), row(1, 1), row(2, 2)}));
}

TEST(ChunkedEvaluation, thresholdCountsPerRowAcrossChunks) {
   const TestIndex first{rows({row(0, 0), row(1, 0), row(2, 0)})};
   const TestIndex second{rows({row(0, 0), row(1, 1), row(2, 0)})};
   const TestIndex negated{rows({row(0, 0), row(1, 0), row(1, 1), row(1, 2)})};

   // At least 2 of: first, second, !negated
   OperatorVector non_negated_children;
   non_negated_children.push_back(first.scan());
   non_negated_children.push_back(second.scan());
   OperatorVector negated_children;
   negated_children.push_back(negated.scan());
   const Threshold under_test(
      std::move(non_negated_children), std::move(negated_children), 2, false, LAYOUT
   );
   EXPECT_EQ(under_test.evaluate().toRoaring(), rows({row(0, 0), row(2, 0)}));
}

TEST(ChunkedEvaluation, thresholdWithOnlyNegatedChildren) {
   const TestIndex first{rows({row(0, 0), row(1, 0)})};
   const TestIndex second{rows({row(0, 0), row(2, 3)})};
   const TestIndex third{rows({row(1, 1)})};

   // Exactly 2 of: !first, !second, !third, i.e. the rows in exactly one of the indexes
   OperatorVector negated_children;
   negated_children.push_back(first.scan());
   negated_children.push_back(second.scan());
   negated_children.push_back(third.scan());
   const Threshold under_test({}, std::move(negated_children), 2, true, LAYOUT);
   EXPECT_EQ(under_test.evaluate().toRoaring(), rows({row(1, 0), row(1, 1), row(2, 3)}));
}

TEST(ChunkedEvaluation, selectionFiltersEachChunk) {
   const Selection under_test(std::make_unique<EvenRowInChunk>(), LAYOUT);
   EXPECT_EQ(
      under_test.evaluate().toRoaring(),
      rows({row(0, 0), row(0, 2), row(0, 4), row(1, 0), row(1, 2), row(2, 0), row(2, 2)})
   );
}

TEST(ChunkedEvaluation, selectionWithChildOperator) {
   const TestIndex child{rows({row(0, 1), row(0, 2), row(1, 0), row(1, 1), row(2, 2), row(2, 3)})};
   const Selection under_test(child.scan(), std::make_unique<EvenRowInChunk>(), LAYOUT);
   EXPECT_EQ(under_test.evaluate().toRoaring(), rows({row(0, 2), row(1, 0), row(2, 2)}));
}
