#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/roaring_util/bitmap_builder.h"
#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::roaring_util {
namespace {

const uint32_t CONTAINER_SIZE = 1 << 16;

// Helper function to create a container from a vector of values
RoaringContainer createContainer(const std::vector<uint16_t>& values) {
   auto container = RoaringContainer::withCapacity(static_cast<int32_t>(values.size()));
   for (const uint16_t value : values) {
      container.addChecked(value);
   }
   return container;
}

class BitmapBuilderByContainerTest : public ::testing::Test {
  protected:
   void TearDown() override {
      // Cleanup any allocated containers if needed
   }
};

TEST_F(BitmapBuilderByContainerTest, EmptyBuilder) {
   BitmapBuilderByContainer builder;
   roaring::Roaring result = std::move(builder).getBitmap();

   EXPECT_EQ(result.cardinality(), 0);
   EXPECT_TRUE(result.isEmpty());
}

TEST_F(BitmapBuilderByContainerTest, SingleContainer) {
   BitmapBuilderByContainer builder;

   std::vector<uint16_t> values = {1, 2, 3, 100, 200};
   const auto container = createContainer(values);

   builder.addContainer(0, RoaringContainerView{container});
   roaring::Roaring result = std::move(builder).getBitmap();

   // Values in tile 0 are just the values themselves
   roaring::Roaring expected{1, 2, 3, 100, 200};
   EXPECT_EQ(result, expected);
}

TEST_F(BitmapBuilderByContainerTest, MultipleContainersDifferentTiles) {
   BitmapBuilderByContainer builder;

   // First container at tile 0
   std::vector<uint16_t> values1 = {1, 2, 3};
   const auto container1 = createContainer(values1);
   builder.addContainer(0, RoaringContainerView{container1});

   // Second container at tile 1
   std::vector<uint16_t> values2 = {10, 20, 30};
   const auto container2 = createContainer(values2);
   builder.addContainer(1, RoaringContainerView{container2});

   // Third container at tile 2
   std::vector<uint16_t> values3 = {100};
   const auto container3 = createContainer(values3);
   builder.addContainer(2, RoaringContainerView{container3});

   roaring::Roaring result = std::move(builder).getBitmap();

   // Tile 0: 0-65535, Tile 1: 65536-131071, Tile 2: 131072-196607
   roaring::Roaring expected{
      1,
      2,
      3,
      CONTAINER_SIZE + 10,
      CONTAINER_SIZE + 20,
      CONTAINER_SIZE + 30,
      (2 * CONTAINER_SIZE) + 100,
   };
   EXPECT_EQ(result, expected);
}

TEST_F(BitmapBuilderByContainerTest, MultipleContainersSameTile) {
   BitmapBuilderByContainer builder;

   // First container at tile 0
   std::vector<uint16_t> values1 = {1, 2, 3};
   const auto container1 = createContainer(values1);
   builder.addContainer(0, RoaringContainerView{container1});

   // Second container at tile 0 (should merge with first)
   std::vector<uint16_t> values2 = {4, 5, 6};
   const auto container2 = createContainer(values2);
   builder.addContainer(0, RoaringContainerView{container2});

   // Third container at tile 0 (should merge again)
   std::vector<uint16_t> values3 = {7, 8, 9};
   const auto container3 = createContainer(values3);
   builder.addContainer(0, RoaringContainerView{container3});

   roaring::Roaring result = std::move(builder).getBitmap();

   roaring::Roaring expected = {1, 2, 3, 4, 5, 6, 7, 8, 9};
   EXPECT_EQ(result, expected);
}

TEST_F(BitmapBuilderByContainerTest, OverlappingValuesInSameTile) {
   BitmapBuilderByContainer builder;

   std::vector<uint16_t> values1 = {1, 2, 3, 4, 5};
   const auto container1 = createContainer(values1);
   builder.addContainer(0, RoaringContainerView{container1});

   std::vector<uint16_t> values2 = {3, 4, 5, 6, 7};
   const auto container2 = createContainer(values2);
   builder.addContainer(0, RoaringContainerView{container2});

   roaring::Roaring result = std::move(builder).getBitmap();

   roaring::Roaring expected = {1, 2, 3, 4, 5, 6, 7};
   EXPECT_EQ(result, expected);
}

TEST_F(BitmapBuilderByContainerTest, AscendingOrder) {
   BitmapBuilderByContainer builder;

   for (uint16_t i = 0; i < 5; ++i) {
      std::vector<uint16_t> values = {static_cast<uint16_t>(i * 10)};
      const auto container = createContainer(values);
      builder.addContainer(i, RoaringContainerView{container});
   }

   roaring::Roaring result = std::move(builder).getBitmap();

   // Each tile contributes one value
   roaring::Roaring expected = {
      0,
      CONTAINER_SIZE + 10,
      (2 * CONTAINER_SIZE) + 20,
      (3 * CONTAINER_SIZE) + 30,
      (4 * CONTAINER_SIZE) + 40,
   };
   EXPECT_EQ(result, expected);
}

TEST_F(BitmapBuilderByContainerTest, LargeContainer) {
   BitmapBuilderByContainer builder;

   std::vector<uint16_t> values;
   values.reserve(CONTAINER_SIZE);
   for (uint32_t i = 0; i < CONTAINER_SIZE; ++i) {
      values.push_back(i);
   }

   const auto container = createContainer(values);
   builder.addContainer(0, RoaringContainerView{container});

   roaring::Roaring result = std::move(builder).getBitmap();

   EXPECT_EQ(result.cardinality(), CONTAINER_SIZE);
   EXPECT_TRUE(result.contains(0));
   EXPECT_TRUE(result.contains(CONTAINER_SIZE - 1));
   EXPECT_FALSE(result.contains(CONTAINER_SIZE));
}

TEST_F(BitmapBuilderByContainerTest, MixedOperations) {
   BitmapBuilderByContainer builder;

   // Tile 0, first batch
   const auto container_1 = createContainer({1, 2, 3});
   builder.addContainer(0, RoaringContainerView{container_1});

   // Tile 0, second batch (merge)
   const auto container_2 = createContainer({4, 5, 6});
   builder.addContainer(0, RoaringContainerView{container_2});

   // Tile 0, third batch (merge)
   const auto container_4 = createContainer({7, 8});
   builder.addContainer(0, RoaringContainerView{container_4});

   // Tile 1
   const auto container_3 = createContainer({10, 20});
   builder.addContainer(1, RoaringContainerView{container_3});

   // Tile 2
   const auto container_5 = createContainer({100});
   builder.addContainer(2, RoaringContainerView{container_5});

   roaring::Roaring result = std::move(builder).getBitmap();

   // Tile 0 should have 1-8, Tile 1 should have 10, 20, Tile 2 should have 100
   EXPECT_EQ(result.cardinality(), 11);
   EXPECT_TRUE(result.contains(1));
   EXPECT_TRUE(result.contains(8));
   EXPECT_TRUE(result.contains(CONTAINER_SIZE + 10));
   EXPECT_TRUE(result.contains(CONTAINER_SIZE + 20));
   EXPECT_TRUE(result.contains((2 * CONTAINER_SIZE) + 100));
}

TEST_F(BitmapBuilderByContainerTest, SingleTileMultipleAdditions) {
   BitmapBuilderByContainer builder;

   // Add multiple batches to tile 5
   for (int batch = 0; batch < 10; ++batch) {
      std::vector<uint16_t> values;
      values.reserve(10);
      for (int i = 0; i < 10; ++i) {
         values.push_back((batch * 10) + i);
      }
      const auto container = createContainer(values);
      builder.addContainer(5, RoaringContainerView{container});
   }

   roaring::Roaring result = std::move(builder).getBitmap();

   EXPECT_EQ(result.cardinality(), 100);

   // All values should be in tile 5 (offset 327680 = 5 * 65536)
   uint32_t base = 5 * CONTAINER_SIZE;
   for (int i = 0; i < 100; ++i) {
      EXPECT_TRUE(result.contains(base + i));
   }
}

}  // namespace
}  // namespace rhydb::roaring_util
