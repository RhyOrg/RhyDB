#include "rhydb/storage/column/lineage_index.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using rhydb::Idx;
using rhydb::common::LineageTree;
using rhydb::common::RecombinantEdgeFollowingMode;
using rhydb::storage::LineageIndex;

/*      v
 *      1
 *     / \
 *    /   \
 *   2     0
 *    \   / \
 *     \ /   \
 *      3     5
 *       \   /
 *        \ /
 *         4
 */

namespace {
LineageTree createDoubleDiamondLineageTree() {
   return LineageTree::fromEdgeList(
      6, {{1, 2}, {2, 3}, {1, 0}, {0, 3}, {3, 4}, {0, 5}, {5, 4}}, {}, {}
   );
}

void assertEqualHelper(std::optional<const roaring::Roaring*>& actual, roaring::Roaring& expected) {
   ASSERT_EQ(actual.has_value(), !expected.isEmpty());
   if (!expected.isEmpty()) {
      ASSERT_EQ(*actual.value(), expected);
   }
}
}  // namespace

TEST(LineageIndex, someTreeValuesCorrectBehavior) {
   auto lineage_tree = createDoubleDiamondLineageTree();
   LineageIndex lineage_index{&lineage_tree};
   lineage_index.insert(0, 1);
   lineage_index.insert(1, 3);
   lineage_index.insert(2, 1);
   lineage_index.insert(3, 2);
   lineage_index.insert(4, 0);
   std::vector<roaring::Roaring> lineages_without_sublineages{{4}, {0, 2}, {3}, {1}, {}, {}};
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterExcludingSublineages(lineage_id);
      auto expected = lineages_without_sublineages.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
   std::vector<roaring::Roaring> lineages_with_sublineages_no_recombinants{
      {4}, {0, 2, 3, 4}, {3}, {1}, {}, {},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterIncludingSublineages(
         lineage_id, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
      );
      auto expected = lineages_with_sublineages_no_recombinants.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
   std::vector<roaring::Roaring> lineages_with_sublineages_contained_recombinants{
      {4}, {0, 2, 3, 4, 1}, {3}, {1}, {}, {},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterIncludingSublineages(
         lineage_id, RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      );
      auto expected = lineages_with_sublineages_contained_recombinants.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
   std::vector<roaring::Roaring> lineages_with_sublineages_all_recombinants{
      {4, 1}, {0, 2, 3, 4, 1}, {3, 1}, {1}, {}, {},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterIncludingSublineages(
         lineage_id, RecombinantEdgeFollowingMode::ALWAYS_FOLLOW
      );
      auto expected = lineages_with_sublineages_all_recombinants.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
}

TEST(LineageIndex, allTreeValuesCorrectBehavior) {
   auto lineage_tree = createDoubleDiamondLineageTree();
   LineageIndex lineage_index{&lineage_tree};
   lineage_index.insert(0, 1);
   lineage_index.insert(1, 3);
   lineage_index.insert(2, 1);
   lineage_index.insert(3, 2);
   lineage_index.insert(4, 0);
   lineage_index.insert(5, 4);
   lineage_index.insert(6, 5);
   lineage_index.insert(7, 4);
   lineage_index.insert(8, 5);
   lineage_index.insert(9, 5);
   std::vector<roaring::Roaring> lineages_without_sublineages{
      {4}, {0, 2}, {3}, {1}, {5, 7}, {6, 8, 9},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterExcludingSublineages(lineage_id);
      auto expected = lineages_without_sublineages.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
   std::vector<roaring::Roaring> lineages_with_sublineages_no_recombinants{
      {4, 6, 8, 9}, {0, 2, 3, 4, 6, 8, 9}, {3}, {1}, {5, 7}, {6, 8, 9},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterIncludingSublineages(
         lineage_id, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
      );
      auto expected = lineages_with_sublineages_no_recombinants.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
   std::vector<roaring::Roaring> lineages_with_sublineages_contained_recombinants{
      {4, 6, 8, 9}, {0, 2, 3, 4, 1, 6, 8, 9, 5, 7}, {3}, {1}, {5, 7}, {6, 8, 9},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterIncludingSublineages(
         lineage_id, RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      );
      auto expected = lineages_with_sublineages_contained_recombinants.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
   std::vector<roaring::Roaring> lineages_with_sublineages_all_recombinants{
      {1, 4, 6, 8, 9, 5, 7},
      {0, 2, 3, 4, 1, 6, 8, 9, 5, 7},
      {1, 3, 5, 7},
      {1, 5, 7},
      {5, 7},
      {6, 8, 9, 5, 7},
   };
   for (Idx lineage_id = 0; lineage_id < lineages_without_sublineages.size(); lineage_id++) {
      auto actual = lineage_index.filterIncludingSublineages(
         lineage_id, RecombinantEdgeFollowingMode::ALWAYS_FOLLOW
      );
      auto expected = lineages_with_sublineages_all_recombinants.at(lineage_id);
      assertEqualHelper(actual, expected);
   }
}