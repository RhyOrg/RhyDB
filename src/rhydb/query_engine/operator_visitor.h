#pragma once

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/bitmap_aggregation_node.h"
#include "rhydb/query_engine/operators/count_filter_node.h"
#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/insertions_node.h"
#include "rhydb/query_engine/operators/join_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/most_recent_common_ancestor_node.h"
#include "rhydb/query_engine/operators/mutations_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/order_by_with_limit_node.h"
#include "rhydb/query_engine/operators/phylo_subtree_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/schema_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/tables_node.h"
#include "rhydb/query_engine/operators/transitive_closure_node.h"
#include "rhydb/query_engine/operators/union_all_node.h"
#include "rhydb/query_engine/operators/unresolved_insertions_node.h"
#include "rhydb/query_engine/operators/unresolved_most_recent_common_ancestor_node.h"
#include "rhydb/query_engine/operators/unresolved_mutations_node.h"
#include "rhydb/query_engine/operators/unresolved_phylo_subtree_node.h"

namespace rhydb::query_engine::operators {

template <typename Func>
// NOLINTNEXTLINE(misc-no-recursion)
decltype(auto) visit(QueryNode& node, Func&& func) {
   switch (node.kind()) {
      case NodeKind::AGGREGATE:
         return std::forward<Func>(func)(static_cast<AggregateNode&>(node));
      case NodeKind::PROJECT:
         return std::forward<Func>(func)(static_cast<ProjectNode&>(node));
      case NodeKind::MAP:
         return std::forward<Func>(func)(static_cast<MapNode&>(node));
      case NodeKind::ORDER_BY:
         return std::forward<Func>(func)(static_cast<OrderByNode&>(node));
      case NodeKind::ORDER_BY_WITH_LIMIT:
         return std::forward<Func>(func)(static_cast<OrderByWithLimitNode&>(node));
      case NodeKind::FETCH:
         return std::forward<Func>(func)(static_cast<FetchNode&>(node));
      case NodeKind::FILTER:
         return std::forward<Func>(func)(static_cast<FilterNode&>(node));
      case NodeKind::UNRESOLVED_MUTATIONS_NUCLEOTIDE:
         return std::forward<Func>(func)(
            static_cast<UnresolvedMutationsNode<rhydb::Nucleotide>&>(node)
         );
      case NodeKind::UNRESOLVED_MUTATIONS_AMINO_ACID:
         return std::forward<Func>(func)(
            static_cast<UnresolvedMutationsNode<rhydb::AminoAcid>&>(node)
         );
      case NodeKind::UNRESOLVED_INSERTIONS_NUCLEOTIDE:
         return std::forward<Func>(func)(
            static_cast<UnresolvedInsertionsNode<rhydb::Nucleotide>&>(node)
         );
      case NodeKind::UNRESOLVED_INSERTIONS_AMINO_ACID:
         return std::forward<Func>(func)(
            static_cast<UnresolvedInsertionsNode<rhydb::AminoAcid>&>(node)
         );
      case NodeKind::UNRESOLVED_MOST_RECENT_COMMON_ANCESTOR:
         return std::forward<Func>(func)(
            static_cast<UnresolvedMostRecentCommonAncestorNode&>(node)
         );
      case NodeKind::UNRESOLVED_PHYLO_SUBTREE:
         return std::forward<Func>(func)(static_cast<UnresolvedPhyloSubtreeNode&>(node));
      case NodeKind::MUTATIONS_NUCLEOTIDE:
         return std::forward<Func>(func)(static_cast<MutationsNode<rhydb::Nucleotide>&>(node));
      case NodeKind::MUTATIONS_AMINO_ACID:
         return std::forward<Func>(func)(static_cast<MutationsNode<rhydb::AminoAcid>&>(node));
      case NodeKind::INSERTIONS_NUCLEOTIDE:
         return std::forward<Func>(func)(static_cast<InsertionsNode<rhydb::Nucleotide>&>(node));
      case NodeKind::INSERTIONS_AMINO_ACID:
         return std::forward<Func>(func)(static_cast<InsertionsNode<rhydb::AminoAcid>&>(node));
      case NodeKind::MOST_RECENT_COMMON_ANCESTOR:
         return std::forward<Func>(func)(static_cast<MostRecentCommonAncestorNode&>(node));
      case NodeKind::PHYLO_SUBTREE:
         return std::forward<Func>(func)(static_cast<PhyloSubtreeNode&>(node));
      case NodeKind::TABLE_SCAN:
         return std::forward<Func>(func)(static_cast<TableScanNode&>(node));
      case NodeKind::COUNT_FILTER:
         return std::forward<Func>(func)(static_cast<CountFilterNode&>(node));
      case NodeKind::UNION_ALL:
         return std::forward<Func>(func)(static_cast<UnionAllNode&>(node));
      case NodeKind::JOIN:
         return std::forward<Func>(func)(static_cast<JoinNode&>(node));
      case NodeKind::SCHEMA:
         return std::forward<Func>(func)(static_cast<SchemaNode&>(node));
      case NodeKind::TABLES_LIST:
         return std::forward<Func>(func)(static_cast<TablesNode&>(node));
      case NodeKind::BITMAP_AGGREGATION:
         return std::forward<Func>(func)(static_cast<BitmapAggregationNode&>(node));
      case NodeKind::TRANSITIVE_CLOSURE:
         return std::forward<Func>(func)(static_cast<TransitiveClosureNode&>(node));
   }
   RHYDB_UNREACHABLE();
}

}  // namespace rhydb::query_engine::operators
