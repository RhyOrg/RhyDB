#pragma once

#include <unordered_map>

#include <boost/serialization/access.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/types.h"

namespace rhydb::storage {

class LineageIndex {
   friend class boost::serialization::access;

   const common::LineageTree* lineage_tree;
   std::unordered_map<Idx, roaring::Roaring> index_excluding_sublineages;
   std::unordered_map<
      rhydb::common::RecombinantEdgeFollowingMode,
      std::unordered_map<Idx, roaring::Roaring>>
      index_including_sublineages;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & index_including_sublineages;
      archive & index_excluding_sublineages;
      // clang-format on
   }

  public:
   explicit LineageIndex(const common::LineageTree* lineage_tree);

   void insert(size_t row_id, Idx value_id);

   [[nodiscard]] std::optional<const roaring::Roaring*> filterIncludingSublineages(
      Idx value_id,
      rhydb::common::RecombinantEdgeFollowingMode recombinant_edge_following_mode
   ) const;

   [[nodiscard]] std::optional<const roaring::Roaring*> filterExcludingSublineages(
      Idx value_id
   ) const;
};

}  // namespace rhydb::storage
