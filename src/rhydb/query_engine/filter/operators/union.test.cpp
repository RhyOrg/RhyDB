#include "rhydb/query_engine/filter/operators/union.h"

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/index_scan.h"

using rhydb::Bitmap;
using rhydb::query_engine::filter::operators::IndexScan;
using rhydb::query_engine::filter::operators::OperatorVector;
using rhydb::query_engine::filter::operators::Union;
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

TEST(OperatorUnion, evaluatesCorrectOnEmptyInput) {
   OperatorVector input;
   const auto row_layout = RowLayout::of(5);

   const Union under_test(std::move(input), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorUnion, evaluatesCorrectOnOneInput) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring({1, 3, 5})}});
   const auto row_layout = RowLayout::of(7);

   OperatorVector input = generateTestInput(test_bitmaps, row_layout);
   const Union under_test(std::move(input), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1, 3, 5}));
}

TEST(OperatorUnion, evaluateShouldReturnCorrectValues1) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(7);

   OperatorVector input = generateTestInput(test_bitmaps, row_layout);
   const Union under_test(std::move(input), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));
}

TEST(OperatorUnion, evaluateShouldReturnCorrectValues2) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 7}), roaring::Roaring({1, 3}), roaring::Roaring({3})}}
   );
   const auto row_layout = RowLayout::of(8);

   OperatorVector input = generateTestInput(test_bitmaps, row_layout);
   const Union under_test(std::move(input), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1, 3, 7}));
}

TEST(OperatorUnion, evaluateShouldReturnCorrectValuesMany) {
   const std::vector<roaring::Roaring> test_bitmaps({{
      roaring::Roaring(),
      roaring::Roaring({3}),
      roaring::Roaring({4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
      roaring::Roaring({2, 4}),
   },});
   const auto row_layout = RowLayout::of(13);

   OperatorVector input = generateTestInput(test_bitmaps, row_layout);
   const Union under_test(std::move(input), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({2, 3, 4}));
}

TEST(OperatorUnion, evaluateShouldReturnCorrectValuesEmptyInput) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring()}});
   const auto row_layout = RowLayout::of(80);

   OperatorVector input = generateTestInput(test_bitmaps, row_layout);
   const Union under_test(std::move(input), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorUnion, correctTypeInfo) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(5);

   OperatorVector input = generateTestInput(test_bitmaps, row_layout);
   const Union under_test(std::move(input), row_layout);

   ASSERT_EQ(under_test.type(), rhydb::query_engine::filter::operators::UNION);
}
