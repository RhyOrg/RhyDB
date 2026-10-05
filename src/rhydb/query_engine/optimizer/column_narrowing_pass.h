#pragma once

#include <vector>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::operators {
class FilterNode;
class TableScanNode;
class AggregateNode;
class ProjectNode;
class MapNode;
class OrderByNode;
class UnionAllNode;
class JoinNode;
class SchemaNode;
class TransitiveClosureNode;
}  // namespace rhydb::query_engine::operators

namespace rhydb::query_engine::optimizer {

/// Optimization pass that narrows the set of columns produced by each node down to those
/// actually required by its parent, pruning unneeded columns from scans and collapsing
/// redundant Project and Map nodes.
///
/// Carries state (`required`) that is mutated while walking the nodes.
class ColumnNarrowingPass : public PipelinePassBase<ColumnNarrowingPass> {
  public:
   using RequiredColumns = std::vector<schema::ColumnIdentifier>;

   RequiredColumns required;

   explicit ColumnNarrowingPass(RequiredColumns required)
       : required(std::move(required)) {}

   static ColumnNarrowingPass makePass(const operators::QueryNodePtr& node);

   using PipelinePassBase<ColumnNarrowingPass>::operator();

   // Shadowing the PipelinePassBase defaults is the intended way to customize a pass.
   // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)
   operators::QueryNodePtr operator()(operators::FilterNode& node);
   operators::QueryNodePtr operator()(operators::TableScanNode& node);
   operators::QueryNodePtr operator()(operators::AggregateNode& node);
   operators::QueryNodePtr operator()(operators::ProjectNode& node);
   operators::QueryNodePtr operator()(operators::MapNode& node);
   operators::QueryNodePtr operator()(operators::OrderByNode& node);
   operators::QueryNodePtr operator()(operators::UnionAllNode& node);
   operators::QueryNodePtr operator()(operators::JoinNode& node);
   operators::QueryNodePtr operator()(operators::SchemaNode& node);
   operators::QueryNodePtr operator()(operators::TransitiveClosureNode& node);
   // NOLINTEND(bugprone-derived-method-shadowing-base-method)
};

}  // namespace rhydb::query_engine::optimizer
