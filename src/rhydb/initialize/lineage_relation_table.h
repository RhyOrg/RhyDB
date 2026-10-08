#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rhydb/common/lineage_tree.h"

namespace rhydb::initialize {

/// One direct parent->child edge of a lineage tree.
/// A recombinant node contributes one row per parent
struct LineageRelationRow {
   std::string lineage;
   std::optional<std::string> parent;
   bool is_recombinant_edge = false;
   std::optional<std::string> recombinant_clade_ancestor;

   bool operator==(const LineageRelationRow& other) const = default;
};

/// Builds the **direct** parent->child edges of a lineage tree: one row per canonical lineage and
/// each of its immediate parents (a recombinant yields several rows), and one root row with an
/// empty parent for each root. Aliases are not emitted as separate lineages — they resolve to their
/// canonical lineage. The transitive closure is derived from these edges at query time rather than
/// materialized here.
[[nodiscard]] std::vector<LineageRelationRow> buildLineageRelationRows(
   const common::LineageTreeAndIdMap& lineage_tree_and_id_map
);

/// The name of the companion table holding the aliases of the lineage column `column_name`. (The
/// relation table itself is named after the column.)
[[nodiscard]] std::string lineageAliasTableName(std::string_view column_name);

/// One alias of a lineage tree, mapped to the canonical lineage it stands for.
struct LineageAliasRow {
   std::string alias;
   std::string lineage;

   bool operator==(const LineageAliasRow& other) const = default;
};

/// Builds one row per alias of a lineage tree, mapping it to its canonical lineage.
[[nodiscard]] std::vector<LineageAliasRow> buildLineageAliasRows(
   const common::LineageTreeAndIdMap& lineage_tree_and_id_map
);

}  // namespace rhydb::initialize
