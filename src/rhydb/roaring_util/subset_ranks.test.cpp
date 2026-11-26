#include "rhydb/roaring_util/subset_ranks.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "rhydb/roaring_util/roaring_container.h"

using rhydb::roaring_util::RoaringContainer;
using rhydb::roaring_util::RoaringContainerView;
using rhydb::roaring_util::roaringSubsetRanks;

namespace {

class RoaringSubsetRanksTest : public ::testing::Test {
  protected:
   // Helper to create a container from a vector of values
   static RoaringContainer createContainer(const std::vector<uint16_t>& values) {
      auto container = RoaringContainer::withCapacity(static_cast<int32_t>(values.size()));
      for (const uint16_t value : values) {
         container.addChecked(value);
      }
      return container;
   }
};

}  // namespace

TEST_F(RoaringSubsetRanksTest, BasicIntersection) {
   // Test the example from the comments: A={3,4,5,7,9}, B={4,5,6,9}
   // Expected ranks: 4->2, 5->3, 9->5

   const auto c_a = createContainer({3, 4, 5, 7, 9});
   const auto c_b = createContainer({4, 5, 6, 9});
   uint32_t base = 0;

   auto result = roaringSubsetRanks(RoaringContainerView{c_a}, RoaringContainerView{c_b}, base);

   ASSERT_EQ(result.size(), 3);
   EXPECT_EQ(result[0], 2);  // rank of 4 in A
   EXPECT_EQ(result[1], 3);  // rank of 5 in A
   EXPECT_EQ(result[2], 5);  // rank of 9 in A
}

TEST_F(RoaringSubsetRanksTest, EmptyIntersection) {
   // Test with no common elements: A={1,2,3}, B={4,5,6}
   // Expected: empty result

   const auto c_a = createContainer({1, 2, 3});
   const auto c_b = createContainer({4, 5, 6});
   uint32_t base = 0;

   auto result = roaringSubsetRanks(RoaringContainerView{c_a}, RoaringContainerView{c_b}, base);

   EXPECT_TRUE(result.empty());
}

TEST_F(RoaringSubsetRanksTest, IdenticalSets) {
   // Test when A and B are identical: A=B={10,20,30,40}
   // Expected ranks: 10->1, 20->2, 30->3, 40->4

   const auto c_a = createContainer({10, 20, 30, 40});
   const auto c_b = createContainer({10, 20, 30, 40});
   uint32_t base = 0;

   auto result = roaringSubsetRanks(RoaringContainerView{c_a}, RoaringContainerView{c_b}, base);

   ASSERT_EQ(result.size(), 4);
   EXPECT_EQ(result[0], 1);  // rank of 10
   EXPECT_EQ(result[1], 2);  // rank of 20
   EXPECT_EQ(result[2], 3);  // rank of 30
   EXPECT_EQ(result[3], 4);  // rank of 40
}

TEST_F(RoaringSubsetRanksTest, SingleElementIntersection) {
   // Test with single common element: A={1,5,10,15,20}, B={10}
   // Expected: rank of 10 in A is 3

   const auto c_a = createContainer({1, 5, 10, 15, 20});
   const auto c_b = createContainer({10});
   uint32_t base = 0;

   auto result = roaringSubsetRanks(RoaringContainerView{c_a}, RoaringContainerView{c_b}, base);

   ASSERT_EQ(result.size(), 1);
   EXPECT_EQ(result[0], 3);  // rank of 10 in A
}
