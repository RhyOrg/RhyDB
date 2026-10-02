#pragma once

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"

namespace rhydb::query_engine::operators {
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
class NodeResolutionPass : public PipelinePassBase<NodeResolutionPass> {
  public:
   using PipelinePassBase<NodeResolutionPass>::operator();

   template <typename SymbolType>
   operators::QueryNodePtr operator()(operators::UnresolvedMutationsNode<SymbolType>& node);
   template <typename SymbolType>
   operators::QueryNodePtr operator()(operators::UnresolvedInsertionsNode<SymbolType>& node);
   operators::QueryNodePtr operator()(operators::UnresolvedMostRecentCommonAncestorNode& node);
   operators::QueryNodePtr operator()(operators::UnresolvedPhyloSubtreeNode& node);
   operators::QueryNodePtr operator()(operators::SchemaNode& node);
};

}  // namespace rhydb::query_engine::optimizer
