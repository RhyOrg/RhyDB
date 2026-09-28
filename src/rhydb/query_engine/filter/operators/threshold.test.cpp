#include "rhydb/query_engine/filter/operators/threshold.h"

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/query_compilation_exception.h"

using rhydb::Bitmap;
using rhydb::query_engine::filter::operators::IndexScan;
using rhydb::query_engine::filter::operators::OperatorVector;
using rhydb::query_engine::filter::operators::Threshold;
using rhydb::storage::column::RowLayout;

namespace {
OperatorVector generateTestInput(
   const std::vector<roaring::Roaring>& bitmaps,
   const RowLayout& row_layout
) {
   OperatorVector result;
   std::ranges::transform(bitmaps, std::back_inserter(result), [&](const auto& bitmap) {
      return std::make_unique<IndexScan>(Bitmap{&bitmap}, row_layout);
   });
   return result;
}
}  // namespace

TEST(OperatorThreshold, evaluatesCorrectOnEmptyInput) {
   OperatorVector non_negated;
   OperatorVector negated;

   ASSERT_THROW(
      const Threshold under_test(
         std::move(non_negated), std::move(negated), 2, true, RowLayout::of()
      ),
      rhydb::query_engine::QueryCompilationException
   );
}

TEST(OperatorThreshold, evaluatesCorrectOnlyNegated) {
   const std::vector<roaring::Roaring> test_negated_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      OperatorVector(), generateTestInput(test_negated_bitmaps, row_layout), 1, true, row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({2}));

   const Threshold under_test_1_or_more(
      OperatorVector(), generateTestInput(test_negated_bitmaps, row_layout), 1, false, row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 2}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValuesNoNegated) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 1, true, row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 2, true, row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({2, 3}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 1, false, row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 2, false, row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValues) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const std::vector<roaring::Roaring> test_negated_bitmaps(
      {{roaring::Roaring(), roaring::Roaring({3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({0}));

   const Threshold under_test_3_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_3_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_4_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_4_exact.evaluate().toRoaring(), roaring::Roaring({2, 3}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_3_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_3_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));

   const Threshold under_test_4_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_4_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValuesManyNegated) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring({1, 2, 3})}});
   const std::vector<roaring::Roaring> test_negated_bitmaps({{
      roaring::Roaring(),
      roaring::Roaring({3}),
      roaring::Roaring({4}),
      roaring::Roaring({2, 4}),
   },});
   const auto row_layout = RowLayout::of(5);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({4}));

   const Threshold under_test_3_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_3_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_4_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_4_exact.evaluate().toRoaring(), roaring::Roaring({0, 2, 3}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4}));

   const Threshold under_test_3_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_3_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_4_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_4_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValuesEmptyInput) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring()}});
   const std::vector<roaring::Roaring> test_negated_bitmaps({{
      roaring::Roaring({3}),
      roaring::Roaring({4}),
      roaring::Roaring({2, 4}),
   },});
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({4}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({2, 3}));

   const Threshold under_test_3_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_3_exact.evaluate().toRoaring(), roaring::Roaring({0, 1}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_3_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_3_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1}));
}

TEST(OperatorThreshold, correctTypeInfo) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 1, true, row_layout
   );

   ASSERT_EQ(under_test.type(), rhydb::query_engine::filter::operators::THRESHOLD);
}
