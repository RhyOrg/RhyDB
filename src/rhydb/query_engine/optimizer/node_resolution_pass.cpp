#include "rhydb/query_engine/optimizer/node_resolution_pass.h"

#include <optional>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/count_filter_node.h"
#include "rhydb/query_engine/operators/insertions_node.h"
#include "rhydb/query_engine/operators/most_recent_common_ancestor_node.h"
#include "rhydb/query_engine/operators/mutations_node.h"
#include "rhydb/query_engine/operators/phylo_subtree_node.h"
#include "rhydb/query_engine/operators/schema_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/unresolved_most_recent_common_ancestor_node.h"
#include "rhydb/query_engine/operators/unresolved_phylo_subtree_node.h"

namespace rhydb::query_engine::optimizer {

namespace {

std::optional<operators::TableScanNode*> getTableScanOrNone(operators::QueryNode& node) {
   auto* scan = dynamic_cast<operators::TableScanNode*>(&node);
   return scan ? std::optional{scan} : std::nullopt;
}

}  // namespace

template <typename SymbolType>
// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr NodeResolutionPass::operator()(
   operators::UnresolvedMutationsNode<SymbolType>& node
) {
   auto scan = getTableScanOrNone(*node.child);
   CHECK_RHYDB_QUERY(scan.has_value(), "mutations() must be applied to a table scan");

   std::vector<schema::ColumnIdentifier> bound_sequence_columns;
   for (const auto& sequence_name : node.sequence_names) {
      auto column_identifier = (*scan)->table->schema->getColumn(sequence_name);
      CHECK_RHYDB_QUERY(
         column_identifier.has_value() && column_identifier.value().type == SymbolType::COLUMN_TYPE,
         "The database does not contain the {} sequence '{}'",
         SymbolType::SYMBOL_NAME,
         sequence_name
      );
      bound_sequence_columns.emplace_back(column_identifier.value());
   }
   if (node.sequence_names.empty()) {
      for (const auto& column_identifier :
           (*scan)->table->schema->template getColumnByType<typename SymbolType::Column>()) {
         bound_sequence_columns.emplace_back(column_identifier);
      }
   }

   std::vector<std::string_view> fields_to_use;
   if (node.fields.empty()) {
      fields_to_use = {
         operators::MutationsNode<SymbolType>::MUTATION_FROM_FIELD_NAME,
         operators::MutationsNode<SymbolType>::MUTATION_TO_FIELD_NAME,
         operators::MutationsNode<SymbolType>::POSITION_FIELD_NAME,
         operators::MutationsNode<SymbolType>::SEQUENCE_FIELD_NAME,
         operators::MutationsNode<SymbolType>::PROPORTION_FIELD_NAME,
         operators::MutationsNode<SymbolType>::COVERAGE_FIELD_NAME,
         operators::MutationsNode<SymbolType>::COUNT_FIELD_NAME,
      };
   } else {
      for (const auto& field_str : node.fields) {
         auto it = std::ranges::find(operators::MutationsNode<SymbolType>::VALID_FIELDS, field_str);
         CHECK_RHYDB_QUERY(
            it != operators::MutationsNode<SymbolType>::VALID_FIELDS.end(),
            "The attribute 'fields' contains an invalid field '{}'. Valid fields are "
            "mutationFrom, mutationTo, position, sequenceName, proportion, coverage, count.",
            field_str
         );
         fields_to_use.push_back(*it);
      }
   }

   return std::make_unique<operators::MutationsNode<SymbolType>>(
      std::move((*scan)->table),
      std::move((*scan)->filter),
      std::move(bound_sequence_columns),
      node.min_proportion,
      std::move(fields_to_use)
   );
}

template <typename SymbolType>
// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr NodeResolutionPass::operator()(
   operators::UnresolvedInsertionsNode<SymbolType>& node
) {
   auto scan = getTableScanOrNone(*node.child);
   CHECK_RHYDB_QUERY(scan.has_value(), "insertions() must be applied to a table scan");

   std::vector<schema::ColumnIdentifier> bound_sequence_columns;
   for (const auto& sequence_name : node.sequence_names) {
      auto column_identifier = (*scan)->table->schema->getColumn(sequence_name);
      CHECK_RHYDB_QUERY(
         column_identifier.has_value() && column_identifier.value().type == SymbolType::COLUMN_TYPE,
         "The database does not contain the {} sequence '{}'",
         SymbolType::SYMBOL_NAME,
         sequence_name
      );
      bound_sequence_columns.emplace_back(column_identifier.value());
   }
   if (node.sequence_names.empty()) {
      for (const auto& column_identifier :
           (*scan)->table->schema->template getColumnByType<typename SymbolType::Column>()) {
         bound_sequence_columns.emplace_back(column_identifier);
      }
   }

   return std::make_unique<operators::InsertionsNode<SymbolType>>(
      std::move((*scan)->table), std::move((*scan)->filter), std::move(bound_sequence_columns)
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr NodeResolutionPass::operator()(operators::AggregateNode& node) {
   propagateToNode(node.child);

   // Full aggregations (COUNT(*) and only a filter below can be optimized)
   if (node.group_by_fields.empty() && node.aggregates.size() == 1 &&
       node.aggregates[0].function == operators::AggregateFunction::COUNT) {
      auto scan = getTableScanOrNone(*node.child);
      if (scan.has_value()) {
         return std::make_unique<operators::CountFilterNode>(
            std::move((*scan)->table), std::move((*scan)->filter), node.aggregates[0].output_name
         );
      }
   }
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr NodeResolutionPass::operator()(operators::UnresolvedPhyloSubtreeNode& node
) {
   auto scan = getTableScanOrNone(*node.child);
   CHECK_RHYDB_QUERY(scan.has_value(), "phyloSubtree() must be applied to a table scan");

   return std::make_unique<operators::PhyloSubtreeNode>(
      std::move((*scan)->table),
      std::move((*scan)->filter),
      std::move(node.column_name),
      node.print_nodes_not_in_tree,
      node.contract_unary_nodes
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr NodeResolutionPass::operator()(
   operators::UnresolvedMostRecentCommonAncestorNode& node
) {
   auto scan = getTableScanOrNone(*node.child);
   CHECK_RHYDB_QUERY(
      scan.has_value(), "mostRecentCommonAncestor() must be applied to a table scan"
   );
   return std::make_unique<operators::MostRecentCommonAncestorNode>(
      std::move((*scan)->table),
      std::move((*scan)->filter),
      std::move(node.column_name),
      node.print_nodes_not_in_tree
   );
}

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr NodeResolutionPass::operator()(operators::SchemaNode& node) {
   // Resolution does not change a node's output schema, so the reported schema is unaffected.
   propagateToNode(node.child);
   return nullptr;
}

template operators::QueryNodePtr NodeResolutionPass::operator()(operators::UnresolvedMutationsNode<
                                                                rhydb::Nucleotide>&);
template operators::QueryNodePtr NodeResolutionPass::operator()(operators::UnresolvedMutationsNode<
                                                                rhydb::AminoAcid>&);
template operators::QueryNodePtr NodeResolutionPass::operator()(operators::UnresolvedInsertionsNode<
                                                                rhydb::Nucleotide>&);
template operators::QueryNodePtr NodeResolutionPass::operator()(operators::UnresolvedInsertionsNode<
                                                                rhydb::AminoAcid>&);

}  // namespace rhydb::query_engine::optimizer
