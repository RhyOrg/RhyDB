#include "rhydb/common/lineage_tree.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/preprocessing/lineage_definition_file.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

using rhydb::Idx;
using rhydb::common::LineageTree;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::common::RecombinantEdgeFollowingMode;
using rhydb::preprocessing::LineageDefinitionFile;

TEST(LineageTreeAndIdMap, correctSimpleTree) {
   auto lineage_definition_file = LineageDefinitionFile::fromYAMLString(R"(
BASE:
  parents: []
CHILD:
  parents:
    - BASE
)");
   auto lineage_tree =
      LineageTreeAndIdMap::fromLineageDefinitionFile(std::move(lineage_definition_file));
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("BASE").has_value());
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("CHILD").has_value());
   ASSERT_FALSE(lineage_tree.lineage_id_lookup_map.getId("Base").has_value());
   ASSERT_FALSE(lineage_tree.lineage_id_lookup_map.getId("base").has_value());

   ASSERT_EQ(
      lineage_tree.lineage_tree
         .getAllParents(
            lineage_tree.lineage_id_lookup_map.getId("CHILD").value(),
            RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
         )
         .size(),
      2
   );
   ASSERT_EQ(
      lineage_tree.lineage_tree.getAllParents(
         lineage_tree.lineage_id_lookup_map.getId("CHILD").value(),
         RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
      ),
      (std::set<Idx>{
         lineage_tree.lineage_id_lookup_map.getId("BASE").value(),
         lineage_tree.lineage_id_lookup_map.getId("CHILD").value(),
      })
   );
}

TEST(LineageTreeAndIdMap, errorOnMissingParent) {
   EXPECT_THAT(
      []() {
         (void
         )LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
some_lineage:
  parents:
    - parent_that_does_not_exist
)"));
      },
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The lineage 'parent_that_does_not_exist' which is specified as the parent of "
         "vertex 'some_lineage' does not have a definition itself."
      ))
   );
}

TEST(LineageTreeAndIdMap, correctTreeRelations) {
   auto lineage_definition_file = LineageDefinitionFile::fromYAMLString(R"(
BASE:
  aliases: [ base_alias ]
  parents: []
CHILD1:
  parents:
    - BASE
CHILD2:
  parents:
    - base_alias
GRANDCHILD1:
  parents:
    - CHILD1
GRANDCHILD2:
  parents:
    - CHILD1
)");
   auto lineage_tree =
      LineageTreeAndIdMap::fromLineageDefinitionFile(std::move(lineage_definition_file));
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("BASE").has_value());
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("CHILD1").has_value());
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("CHILD2").has_value());
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("GRANDCHILD1").has_value());
   ASSERT_TRUE(lineage_tree.lineage_id_lookup_map.getId("GRANDCHILD2").has_value());
   auto base = lineage_tree.lineage_id_lookup_map.getId("BASE").value();
   auto child1 = lineage_tree.lineage_id_lookup_map.getId("CHILD1").value();
   auto child2 = lineage_tree.lineage_id_lookup_map.getId("CHILD2").value();
   auto grandchild1 = lineage_tree.lineage_id_lookup_map.getId("GRANDCHILD1").value();
   auto grandchild2 = lineage_tree.lineage_id_lookup_map.getId("GRANDCHILD2").value();

   auto ancestors_of_grandchild1 = lineage_tree.lineage_tree.getAllParents(
      grandchild1, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
   );
   ASSERT_EQ(ancestors_of_grandchild1, (std::set<Idx>{base, child1, grandchild1}));
   auto ancestors_of_grandchild2 = lineage_tree.lineage_tree.getAllParents(
      grandchild2, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
   );
   ASSERT_EQ(ancestors_of_grandchild2, (std::set<Idx>{base, child1, grandchild2}));
   auto ancestors_of_child1 =
      lineage_tree.lineage_tree.getAllParents(child1, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW);
   ASSERT_EQ(ancestors_of_child1, (std::set<Idx>{base, child1}));
   auto ancestors_of_child2 =
      lineage_tree.lineage_tree.getAllParents(child2, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW);
   ASSERT_EQ(ancestors_of_child2, (std::set<Idx>{base, child2}));
   auto ancestors_of_base =
      lineage_tree.lineage_tree.getAllParents(base, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW);
   ASSERT_EQ(ancestors_of_base, (std::set<Idx>{base}));
}

TEST(LineageTreeAndIdMap, correctCycleErrorInFile) {
   auto throwing_lambda = []() {
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
BASE:
  parents:
   - CHILD
CHILD:
  parents:
    - BASE
)"));
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(
         ::testing::HasSubstr("The given LineageTree contains the cycle: BASE -> CHILD -> BASE")
      )
   );
}

TEST(LineageTreeAndIdMap, correctSelfCycleErrorInFile) {
   auto throwing_lambda = []() {
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
BASE:
  parents:
   - BASE
CHILD:
  parents:
    - BASE
)"));
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The given LineageTree contains the cycle: BASE -> BASE"

      ))
   );
}

TEST(LineageTreeAndIdMap, correctLassoCycleErrorInFile) {
   auto throwing_lambda = []() {
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
BASE: {}
CHILD1:
  parents:
    - BASE
    - CHILD3
CHILD2:
  parents:
    - CHILD1
CHILD3:
  parents:
    - CHILD2
)"));
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The given LineageTree contains the cycle: CHILD1 -> CHILD2 -> CHILD3 -> CHILD1"
      ))
   );
}

TEST(LineageDefinitionFile, errorOnDuplicateKey) {
   auto throwing_lambda = []() {
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
some_duplicate_lineage:
  parents:
  - some_other_key
some_other_key:
some_duplicate_lineage:
  parents:
  - some_other_key)"));
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The lineage definitions contain the duplicate lineage 'some_duplicate_lineage'"

      ))
   );
}

TEST(LineageDefinitionFile, errorOnDuplicateAlias) {
   auto throwing_lambda = []() {
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
lineage1:
  aliases:
  - duplicate_alias
  parents:
  - some_other_key
lineage2:
lineage3:
  aliases:
  - duplicate_alias
  parents:
  - some_other_key)"));
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The alias 'duplicate_alias' for lineage 'lineage3' is already defined as a lineage "
         "or another alias."
      ))
   );
}

TEST(LineageDefinitionFile, errorOnLineageAsAlias) {
   auto throwing_lambda = []() {
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
lineage1:
  aliases:
  - some_alias
  parents:
  - some_other_key
lineage2_also_used_as_alias:
lineage3:
  aliases:
  - lineage2_also_used_as_alias
  parents:
  - some_other_key)"));
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The alias 'lineage2_also_used_as_alias' for lineage 'lineage3' is already defined "
         "as a lineage or another alias."
      ))
   );
}

TEST(containsCycle, doesNotFindCycleInPangoLineageTree) {
   ASSERT_NO_THROW(LineageTreeAndIdMap::fromLineageDefinitionFilePath(
      "testBaseData/exampleDataset/lineage_definition.yaml"
   ));
}

TEST(containsCycle, doesNotFindCycleInMediumSizedChainGraph) {
   std::vector<std::pair<uint32_t, uint32_t>> chain_edges;
   const uint32_t number_of_edges = UINT16_MAX;
   chain_edges.reserve(number_of_edges);
   for (size_t i = 0; i + 1 < number_of_edges; i++) {
      chain_edges.emplace_back(i, i + 1);
   }
   ASSERT_FALSE(rhydb::common::containsCycle(number_of_edges, chain_edges));
}

TEST(containsCycle, findsCycles) {
   // 1. Simple cycle
   ASSERT_TRUE(rhydb::common::containsCycle(3, {{0, 1}, {1, 0}}));

   // 2. Graph with a cycle and disconnected edges
   ASSERT_TRUE(rhydb::common::containsCycle(5, {{0, 1}, {1, 2}, {2, 0}, {3, 4}}));

   // 3. Complete graph of 3 nodes (cycle)
   ASSERT_TRUE(rhydb::common::containsCycle(3, {{0, 1}, {1, 2}, {2, 0}}));

   // 4. Cycle in a large graph
   ASSERT_TRUE(rhydb::common::containsCycle(6, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}}));

   // 5. Disconnected cycle components
   ASSERT_TRUE(rhydb::common::containsCycle(7, {{0, 1}, {1, 2}, {2, 3}, {4, 5}, {5, 6}, {6, 4}}));

   // 6. Self-loop, cycle present
   ASSERT_TRUE(rhydb::common::containsCycle(3, {{0, 1}, {1, 2}, {2, 2}}));

   // 7. 4 nodes forming two separate cycles
   ASSERT_TRUE(rhydb::common::containsCycle(4, {{0, 1}, {1, 0}, {2, 3}, {3, 2}}));

   // 8. Tree structure with additional edge forming a cycle
   ASSERT_TRUE(rhydb::common::containsCycle(6, {{0, 1}, {0, 2}, {1, 3}, {4, 1}, {3, 5}, {5, 4}}));

   // 9. Fully connected graph of 4 nodes (cycle present)
   ASSERT_TRUE(rhydb::common::containsCycle(4, {{0, 1}, {2, 0}, {0, 3}, {1, 2}, {3, 1}, {2, 3}}));

   // 10. Graph with multiple isolated cycles
   ASSERT_TRUE(
      rhydb::common::containsCycle(8, {{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {6, 7}})
   );

   // 11. Single node with a self-loop (directed cycle)
   ASSERT_TRUE(rhydb::common::containsCycle(1, {{0, 0}}));
}

TEST(containsCycle, correctTrees) {
   // 1. No edges, no cycle
   ASSERT_FALSE(rhydb::common::containsCycle(5, {}));

   // 2. Single edge, no cycle
   ASSERT_FALSE(rhydb::common::containsCycle(2, {{0, 1}}));

   // 3. Linear chain of edges, no cycle
   ASSERT_FALSE(rhydb::common::containsCycle(4, {{0, 1}, {1, 2}, {2, 3}}));

   // 4. Multiple disconnected edges, no cycle
   ASSERT_FALSE(rhydb::common::containsCycle(6, {{0, 1}, {2, 3}, {4, 5}}));

   // 5. Tree structure, no cycle
   ASSERT_FALSE(rhydb::common::containsCycle(5, {{0, 1}, {0, 2}, {1, 3}, {1, 4}}));

   // 6. Graph with a single node and no edges
   ASSERT_FALSE(rhydb::common::containsCycle(1, {}));

   // 7. Two disconnected trees (no cycle)
   ASSERT_FALSE(rhydb::common::containsCycle(6, {{0, 1}, {0, 2}, {3, 4}, {3, 5}}));

   // 8. Star-shaped graph, no cycle
   ASSERT_FALSE(rhydb::common::containsCycle(5, {{0, 1}, {0, 2}, {0, 3}, {0, 4}}));

   // 9. Two disconnected nodes
   ASSERT_FALSE(rhydb::common::containsCycle(2, {}));

   // 10. Chain of 5 nodes
   ASSERT_FALSE(rhydb::common::containsCycle(5, {{0, 1}, {1, 2}, {2, 3}, {3, 4}}));
}

TEST(containsCycle, correctDirectedAcyclicGraphs) {
   // 1. Undirected lasso, but no directed cycle
   ASSERT_FALSE(rhydb::common::containsCycle(6, {{0, 1}, {0, 2}, {1, 3}, {1, 4}, {3, 5}, {4, 5}}));

   // 2. Chain of 5 nodes and first to last shortcut
   ASSERT_FALSE(rhydb::common::containsCycle(5, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {0, 4}}));
}

/*        v
 *        1
 *     /     \
 *    /       \
 *  2          0
 *    \       /
 *     \     /
 *        3
 */

namespace {
LineageTree createDiamondLineageTree() {
   return LineageTree::fromEdgeList(4, {{1, 2}, {2, 3}, {1, 0}, {0, 3}}, {}, {});
}
}  // namespace

TEST(LineageTree, correctLeastCommonAncestorOfRecombinantSimple) {
   auto lineage_tree = createDiamondLineageTree();
   std::unordered_map<Idx, std::optional<Idx>> correct_lca{{3, {1}}};
   ASSERT_EQ(
      LineageTree::computeRecombinantCladeAncestors(lineage_tree.getChildToParentRelation()),
      correct_lca
   );
}

TEST(LineageTree, correctAncestorsInRecombinantGraph) {
   auto lineage_tree = createDiamondLineageTree();
   std::set<Idx> ancestors_of_0{0, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(0, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_0
   );
   std::set<Idx> ancestors_of_1{1};
   ASSERT_EQ(
      lineage_tree.getAllParents(1, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_1
   );
   std::set<Idx> ancestors_of_2{2, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(2, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_2
   );
   std::set<Idx> ancestors_of_3{3};
   ASSERT_EQ(
      lineage_tree.getAllParents(3, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_3
   );
}

TEST(LineageTree, correctAncestorsInRecombinantGraphWithAllRecombinantEdges) {
   auto lineage_tree = createDiamondLineageTree();
   std::set<Idx> ancestors_of_0{0, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(0, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_0
   );
   std::set<Idx> ancestors_of_1{1};
   ASSERT_EQ(
      lineage_tree.getAllParents(1, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_1
   );
   std::set<Idx> ancestors_of_2{2, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(2, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_2
   );
   std::set<Idx> ancestors_of_3{0, 1, 2, 3};
   ASSERT_EQ(
      lineage_tree.getAllParents(3, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_3
   );
}

TEST(LineageTree, correctAncestorsInRecombinantGraphWithCladeRecombinantEdges) {
   auto lineage_tree = createDiamondLineageTree();
   std::set<Idx> ancestors_of_0{0, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         0, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_0
   );
   std::set<Idx> ancestors_of_1{1};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         1, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_1
   );
   std::set<Idx> ancestors_of_2{2, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         2, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_2
   );
   std::set<Idx> ancestors_of_3{1, 3};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         3, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_3
   );
}

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
}  // namespace

TEST(LineageTree, correctLeastCommonAncestorOfRecombinantComplex) {
   auto lineage_tree = createDoubleDiamondLineageTree();
   std::unordered_map<Idx, std::optional<Idx>> correct_lca{{3, {1}}, {4, {1}}};
   ASSERT_EQ(
      LineageTree::computeRecombinantCladeAncestors(lineage_tree.getChildToParentRelation()),
      correct_lca
   );
}

TEST(LineageTree, correctAncestorsInRecombinantGraphComplex) {
   auto lineage_tree = createDoubleDiamondLineageTree();
   std::set<Idx> ancestors_of_0{0, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(0, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_0
   );
   std::set<Idx> ancestors_of_1{1};
   ASSERT_EQ(
      lineage_tree.getAllParents(1, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_1
   );
   std::set<Idx> ancestors_of_2{2, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(2, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_2
   );
   std::set<Idx> ancestors_of_3{3};
   ASSERT_EQ(
      lineage_tree.getAllParents(3, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_3
   );
   std::set<Idx> ancestors_of_4{4};
   ASSERT_EQ(
      lineage_tree.getAllParents(4, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_4
   );
   std::set<Idx> ancestors_of_5{0, 1, 5};
   ASSERT_EQ(
      lineage_tree.getAllParents(5, RecombinantEdgeFollowingMode::DO_NOT_FOLLOW), ancestors_of_5
   );
}

TEST(LineageTree, correctAncestorsInRecombinantGraphWithAllRecombinantEdgesComplex) {
   auto lineage_tree = createDoubleDiamondLineageTree();
   std::set<Idx> ancestors_of_0{0, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(0, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_0
   );
   std::set<Idx> ancestors_of_1{1};
   ASSERT_EQ(
      lineage_tree.getAllParents(1, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_1
   );
   std::set<Idx> ancestors_of_2{2, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(2, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_2
   );
   std::set<Idx> ancestors_of_3{0, 1, 2, 3};
   ASSERT_EQ(
      lineage_tree.getAllParents(3, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_3
   );
   std::set<Idx> ancestors_of_4{0, 1, 2, 3, 4, 5};
   ASSERT_EQ(
      lineage_tree.getAllParents(4, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_4
   );
   std::set<Idx> ancestors_of_5{0, 1, 5};
   ASSERT_EQ(
      lineage_tree.getAllParents(5, rhydb::common::RecombinantEdgeFollowingMode::ALWAYS_FOLLOW),
      ancestors_of_5
   );
}

TEST(LineageTree, correctAncestorsInRecombinantGraphWithCladeRecombinantEdgesComplex) {
   auto lineage_tree = createDoubleDiamondLineageTree();
   std::set<Idx> ancestors_of_0{0, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         0, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_0
   );
   std::set<Idx> ancestors_of_1{1};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         1, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_1
   );
   std::set<Idx> ancestors_of_2{2, 1};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         2, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_2
   );
   std::set<Idx> ancestors_of_3{1, 3};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         3, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_3
   );
   std::set<Idx> ancestors_of_4{1, 4};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         4, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_4
   );
   std::set<Idx> ancestors_of_5{0, 1, 5};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         5, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_5
   );
}

/*   v     v
 *   2     0
 *    \   / \
 *     \ /   \
 *      3     1
 *       \   /
 *        \ /
 *         4
 */

namespace {

LineageTree createDiamondLineageTreeWithTwoRoots() {
   return LineageTree::fromEdgeList(6, {{2, 3}, {0, 3}, {3, 4}, {0, 1}, {1, 4}}, {}, {});
}
}  // namespace

TEST(LineageTree, noLeastCommonAncestor) {
   auto lineage_tree = createDiamondLineageTreeWithTwoRoots();
   std::unordered_map<Idx, std::optional<Idx>> correct_lca{{3, std::nullopt}, {4, std::nullopt}};
   ASSERT_EQ(
      LineageTree::computeRecombinantCladeAncestors(lineage_tree.getChildToParentRelation()),
      correct_lca
   );
}

TEST(LineageTree, correctlyHasNoAncestors) {
   auto lineage_tree = createDiamondLineageTreeWithTwoRoots();
   std::set<Idx> ancestors_of_4{4};
   ASSERT_EQ(
      lineage_tree.getAllParents(
         4, rhydb::common::RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
      ),
      ancestors_of_4
   );
}
