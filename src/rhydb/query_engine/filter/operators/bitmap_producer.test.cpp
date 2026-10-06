#include "rhydb/query_engine/filter/operators/bitmap_producer.h"

#include "rhydb/common/bitmap.h"

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

using rhydb::Bitmap;
using rhydb::query_engine::filter::operators::BitmapProducer;
using rhydb::storage::column::RowLayout;

TEST(OperatorBitmapProducer, evaluateShouldReturnCorrectValues) {
   const roaring::Roaring test_bitmap({1, 2, 3});
   const auto row_layout = RowLayout::of(5);

   const BitmapProducer under_test([&] { return Bitmap(&test_bitmap); }, row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));
}

TEST(OperatorBitmapProducer, evaluateShouldReturnCorrectValuesWhenNegated) {
   const roaring::Roaring test_bitmap({1, 2, 3});
   const auto row_layout = RowLayout::of(5);

   auto under_test =
      std::make_unique<BitmapProducer>([&] { return Bitmap(&test_bitmap); }, row_layout);
   const auto negated = BitmapProducer::negate(std::move(under_test));

   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({0, 4}));
}

TEST(OperatorBitmapProducer, correctTypeInfo) {
   const roaring::Roaring test_bitmap({1, 2, 3});
   const auto row_layout = RowLayout::of(5);

   const BitmapProducer under_test([&] { return Bitmap(&test_bitmap); }, row_layout);
   ASSERT_EQ(under_test.type(), rhydb::query_engine::filter::operators::BITMAP_PRODUCER);
}

TEST(OperatorBitmapProducer, correctToString) {
   const roaring::Roaring test_bitmap({1, 2, 3});
   const auto row_layout = RowLayout::of(5);

   const BitmapProducer under_test([&] { return Bitmap(&test_bitmap); }, row_layout);
   ASSERT_EQ(under_test.toString(), "BitmapProducer");
}