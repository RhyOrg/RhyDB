#include "rhydb/initialize/lineage_relation_table.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "rhydb/common/types.h"

namespace rhydb::initialize {

std::vector<LineageRelationRow> buildLineageRelationRows(
   const common::LineageTreeAndIdMap& lineage_tree_and_id_map
) {
   const auto& tree = lineage_tree_and_id_map.lineage_tree;
   const auto& names = lineage_tree_and_id_map.lineage_id_lookup_map;
   const auto& child_to_parent = tree.getChildToParentRelation();
   const auto& clade_ancestors = tree.getRecombinantCladeAncestors();

   // child_to_parent is indexed by canonical lineage id (aliases are assigned separate ids beyond
   // this range and are resolved to their canonical lineage rather than emitted as edges).
   std::vector<LineageRelationRow> rows;
   for (size_t index = 0; index < child_to_parent.size(); ++index) {
      const auto child_id = static_cast<Idx>(index);
      std::string lineage{names.getValue(child_id)};
      const auto& parents = child_to_parent[index];

      if (parents.empty()) {
         // A root: no parent edge, but the lineage still needs a row so the walk can terminate and
         // so a lineage carrying no sequences is still present in the table.
         rows.push_back(
            {.lineage = std::move(lineage),
             .parent = std::nullopt,
             .is_recombinant_edge = false,
             .recombinant_clade_ancestor = std::nullopt,}
         );
         continue;
      }

      const bool is_recombinant = parents.size() > 1;
      std::optional<std::string> recombinant_clade_ancestor;
      if (is_recombinant) {
         if (const auto iterator = clade_ancestors.find(child_id);
             iterator != clade_ancestors.end() && iterator->second.has_value()) {
            recombinant_clade_ancestor = std::string{names.getValue(iterator->second.value())};
         }
      }
      for (const Idx parent_id : parents) {
         rows.push_back(
            {.lineage = lineage,
             .parent = std::string{names.getValue(parent_id)},
             .is_recombinant_edge = is_recombinant,
             .recombinant_clade_ancestor = recombinant_clade_ancestor,}
         );
      }
   }
   return rows;
}

}  // namespace rhydb::initialize
