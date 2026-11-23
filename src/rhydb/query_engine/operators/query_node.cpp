#include "rhydb/query_engine/operators/query_node.h"

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::operators {

std::string_view nodeKindToString(NodeKind kind) {
   switch (kind) {
      case NodeKind::AGGREGATE:
         return "Aggregate";
      case NodeKind::PROJECT:
         return "Project";
      case NodeKind::ORDER_BY:
         return "OrderBy";
      case NodeKind::ORDER_BY_WITH_LIMIT:
         return "OrderByWithLimit";
      case NodeKind::FETCH:
         return "Fetch";
      case NodeKind::FILTER:
         return "Filter";
      case NodeKind::UNRESOLVED_MUTATIONS_NUCLEOTIDE:
         return "UnresolvedMutationsNucleotide";
      case NodeKind::UNRESOLVED_MUTATIONS_AMINO_ACID:
         return "UnresolvedMutationsAminoAcid";
      case NodeKind::UNRESOLVED_INSERTIONS_NUCLEOTIDE:
         return "UnresolvedInsertionsNucleotide";
      case NodeKind::UNRESOLVED_INSERTIONS_AMINO_ACID:
         return "UnresolvedInsertionsAminoAcid";
      case NodeKind::UNRESOLVED_MOST_RECENT_COMMON_ANCESTOR:
         return "UnresolvedMostRecentCommonAncestor";
      case NodeKind::UNRESOLVED_PHYLO_SUBTREE:
         return "UnresolvedPhyloSubtree";
      case NodeKind::MUTATIONS_NUCLEOTIDE:
         return "MutationsNucleotide";
      case NodeKind::MUTATIONS_AMINO_ACID:
         return "MutationsAminoAcid";
      case NodeKind::INSERTIONS_NUCLEOTIDE:
         return "InsertionsNucleotide";
      case NodeKind::INSERTIONS_AMINO_ACID:
         return "InsertionsAminoAcid";
      case NodeKind::MOST_RECENT_COMMON_ANCESTOR:
         return "MostRecentCommonAncestor";
      case NodeKind::PHYLO_SUBTREE:
         return "PhyloSubtree";
      case NodeKind::TABLE_SCAN:
         return "TableScan";
      case NodeKind::COUNT_FILTER:
         return "CountFilter";
      case NodeKind::MAP:
         return "Map";
      case NodeKind::UNION_ALL:
         return "UnionAll";
      case NodeKind::JOIN:
         return "Join";
      case NodeKind::SCHEMA:
         return "Schema";
      case NodeKind::TABLES_LIST:
         return "TablesList";
      case NodeKind::TABLE_LITERAL:
         return "TableLiteral";
      case NodeKind::BITMAP_AGGREGATION:
         return "BitmapAggregation";
      case NodeKind::TRANSITIVE_CLOSURE:
         return "TransitiveClosure";
   }
   RHYDB_UNREACHABLE();
}

nlohmann::json columnToJson(const schema::ColumnIdentifier& column) {
   return {
      {"name", column.name},
      {"type", schema::dataTypeToString(column.type)},
   };
}

nlohmann::json columnsToJson(const std::vector<schema::ColumnIdentifier>& columns) {
   nlohmann::json result = nlohmann::json::array();
   for (const auto& column : columns) {
      result.push_back(columnToJson(column));
   }
   return result;
}

}  // namespace rhydb::query_engine::operators
