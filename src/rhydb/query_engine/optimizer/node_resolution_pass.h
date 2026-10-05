#pragma once

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"

namespace rhydb::query_engine::operators {
class AggregateNode;
template <typename SymbolType>
class UnresolvedMutationsNode;
template <typename SymbolType>
class UnresolvedInsertionsNode;
class UnresolvedMostRecentCommonAncestorNode;
class UnresolvedPhyloSubtreeNode;
class SchemaNode;
}  // namespace rhydb::query_engine::operators

namespace rhydb::query_engine::optimizer {

/// Optimization pass that resolves placeholder nodes into concrete, table-backed nodes.
/// - UnresolvedMutationsNode → MutationsNode
/// - UnresolvedInsertionsNode → InsertionsNode
/// - UnresolvedPhyloSubtreeNode → PhyloSubtreeNode
/// - UnresolvedMostRecentCommonAncestorNode → MostRecentCommonAncestorNode
/// - AggregateNode(COUNT(*), TableScanNode) → CountFilterNode
class NodeResolutionPass : public PipelinePassBase<NodeResolutionPass> {
  public:
   using PipelinePassBase<NodeResolutionPass>::operator();

   // Shadowing the PipelinePassBase defaults is the intended way to customize a pass.
   // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)
   operators::QueryNodePtr operator()(operators::AggregateNode& node);
   template <typename SymbolType>
   operators::QueryNodePtr operator()(operators::UnresolvedMutationsNode<SymbolType>& node);
   template <typename SymbolType>
   operators::QueryNodePtr operator()(operators::UnresolvedInsertionsNode<SymbolType>& node);
   operators::QueryNodePtr operator()(operators::UnresolvedMostRecentCommonAncestorNode& node);
   operators::QueryNodePtr operator()(operators::UnresolvedPhyloSubtreeNode& node);
   operators::QueryNodePtr operator()(operators::SchemaNode& node);
   // NOLINTEND(bugprone-derived-method-shadowing-base-method)
};

}  // namespace rhydb::query_engine::optimizer
