#include "rhydb/storage/column/lineage_index.h"

#include <spdlog/spdlog.h>

namespace rhydb::storage {

using rhydb::common::ALL_RECOMBINANT_EDGE_FOLLOWING_MODES;
using rhydb::common::RecombinantEdgeFollowingMode;

LineageIndex::LineageIndex(const common::LineageTree* lineage_tree)
    : lineage_tree(lineage_tree) {
   for (auto mode : ALL_RECOMBINANT_EDGE_FOLLOWING_MODES) {
      index_including_sublineages.emplace(mode, std::unordered_map<Idx, roaring::Roaring>{});
   }
}

void LineageIndex::insert(size_t row_id, Idx value_id) {
   value_id = lineage_tree->resolveAlias(value_id);
   index_excluding_sublineages[value_id].add(row_id);
   for (auto mode : ALL_RECOMBINANT_EDGE_FOLLOWING_MODES) {
      for (auto lineage : lineage_tree->getAllParents(value_id, mode)) {
         index_including_sublineages.at(mode)[lineage].add(row_id);
      }
   }
}

std::optional<const roaring::Roaring*> LineageIndex::filterIncludingSublineages(
   Idx value_id,
   RecombinantEdgeFollowingMode recombinant_edge_following_mode
) const {
   value_id = lineage_tree->resolveAlias(value_id);
   if (index_including_sublineages.at(recombinant_edge_following_mode).contains(value_id)) {
      return &index_including_sublineages.at(recombinant_edge_following_mode).at(value_id);
   }
   return std::nullopt;
}

std::optional<const roaring::Roaring*> LineageIndex::filterExcludingSublineages(
   Idx value_id
) const {
   value_id = lineage_tree->resolveAlias(value_id);
   if (index_excluding_sublineages.contains(value_id)) {
      return &index_excluding_sublineages.at(value_id);
   }
   return std::nullopt;
}

}  // namespace rhydb::storage
