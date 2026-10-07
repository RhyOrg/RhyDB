#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/serialization/access.hpp>

#include "rhydb/common/bidirectional_string_map.h"
#include "rhydb/common/types.h"
#include "rhydb/preprocessing/lineage_definition_file.h"

namespace rhydb::common {

/*
 *  subtree(A.1, DO_NOT_FOLLOW) returns A.1
 *  subtree(A.1, ALWAYS_FOLLOW) returns A.1, XBB
 *  subtree(A.1, FOLLOW_IF_FULLY_CONTAINED_IN_CLADE) returns A.1
 *  subtree(A, DO_NOT_FOLLOW) returns A, A.1, A.2
 *  subtree(A, ALWAYS_FOLLOW) returns A, A.1, A.2, XBB
 *  subtree(A, FOLLOW_IF_FULLY_CONTAINED_IN_CLADE) returns A, A.1, A.2, _XBB_
 *        v
 *        A
 *     /     \
 *    /       \
 *  A.1       A.2
 *    \       /
 *     \     /
 *       XBB
 */

enum class RecombinantEdgeFollowingMode : uint8_t {
   DO_NOT_FOLLOW,
   FOLLOW_IF_FULLY_CONTAINED_IN_CLADE,
   ALWAYS_FOLLOW,
};

const std::vector<RecombinantEdgeFollowingMode> ALL_RECOMBINANT_EDGE_FOLLOWING_MODES{
   RecombinantEdgeFollowingMode::DO_NOT_FOLLOW,
   RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE,
   RecombinantEdgeFollowingMode::ALWAYS_FOLLOW,
};

// The tree is allowed to be disconnected
class LineageTree {
   friend class boost::serialization::access;
   std::vector<std::vector<Idx>> child_to_parent_relation;
   // These edges are the least common ancestor of the parents of a recombinant node
   // We save these because then we do not need to recompute which nodes can reach a given
   // recombinant node
   std::unordered_map<Idx, std::optional<Idx>> recombinant_clade_ancestors;
   std::unordered_map<Idx, Idx> alias_mapping;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & child_to_parent_relation;
      archive & recombinant_clade_ancestors;
      archive & alias_mapping;
      // clang-format on
   }

  public:
   LineageTree() = default;
   LineageTree(LineageTree&& other) = default;
   LineageTree(const LineageTree& other) = default;
   LineageTree& operator=(const LineageTree& other) = default;
   LineageTree& operator=(LineageTree&& other) = default;

   [[nodiscard]] const std::vector<std::vector<Idx>>& getChildToParentRelation() const {
      return child_to_parent_relation;
   }

   /// For every recombinant node (a node with more than one parent), the most-recent common
   /// ancestor of its parents (or `nullopt` when the parents share no common ancestor). Nodes that
   /// are not recombinant do not appear. Used by the `FOLLOW_IF_FULLY_CONTAINED_IN_CLADE` walk.
   [[nodiscard]] const std::unordered_map<Idx, std::optional<Idx>>& getRecombinantCladeAncestors(
   ) const {
      return recombinant_clade_ancestors;
   }

   static std::unordered_map<Idx, std::optional<Idx>> computeRecombinantCladeAncestors(
      const std::vector<std::vector<Idx>>& child_to_parent_relation
   );

   static LineageTree fromEdgeList(
      size_t n_vertices,
      const std::vector<std::pair<Idx, Idx>>& edge_list,
      const BidirectionalStringMap& lookup,
      std::unordered_map<Idx, Idx>&& alias_mapping
   );

   [[nodiscard]] std::set<Idx> getAllParents(
      Idx value_id,
      RecombinantEdgeFollowingMode follow_recombinant_edges
   ) const;

   [[nodiscard]] Idx resolveAlias(Idx value_id) const;
};

class LineageTreeAndIdMap {
  public:
   LineageTree lineage_tree;
   BidirectionalStringMap lineage_id_lookup_map;
   std::string file;

   LineageTreeAndIdMap() = default;
   LineageTreeAndIdMap(LineageTreeAndIdMap&& other) = default;
   LineageTreeAndIdMap& operator=(LineageTreeAndIdMap&& other) = default;
   LineageTreeAndIdMap(const LineageTreeAndIdMap& other);
   LineageTreeAndIdMap& operator=(const LineageTreeAndIdMap& other);

   static LineageTreeAndIdMap fromLineageDefinitionFile(
      rhydb::preprocessing::LineageDefinitionFile&& file
   );

   static LineageTreeAndIdMap fromLineageDefinitionFilePath(const std::filesystem::path& file_path);

  private:
   LineageTreeAndIdMap(
      LineageTree&& lineage_tree,
      BidirectionalStringMap&& lineage_id_lookup_map,
      std::string&& file
   );

   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & lineage_tree;
      archive & lineage_id_lookup_map;
      archive & file;
      // clang-format on
   }
};

std::optional<std::vector<Idx>> containsCycle(
   size_t number_of_vertices,
   const std::vector<std::pair<Idx, Idx>>& edges
);

}  // namespace rhydb::common
