#pragma once

#include <map>
#include <memory>
#include <vector>

#include <arrow/result.h>
#include <nlohmann/json_fwd.hpp>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

/// Extends its child's output with additional columns. Each added column is
/// assigned a scalar expression (e.g. `x := 3`). All of the child's columns are
/// passed through unchanged; an assignment whose output name matches an
/// existing column replaces that column in place.
class MapNode final : public QueryNode {
  public:
   struct Assignment {
      schema::ColumnIdentifier output_column;
      std::unique_ptr<scalar_expressions::ScalarExpression> expression;
   };

   QueryNodePtr child;
   std::vector<Assignment> assignments;

   MapNode(QueryNodePtr child, std::vector<Assignment> assignments);

   [[nodiscard]] std::vector<schema::ColumnIdentifier> getOutputSchema() const override;

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const override;

   [[nodiscard]] NodeKind kind() const override { return NodeKind::MAP; }

   [[nodiscard]] nlohmann::json toJson() const override;

  private:
   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addProjection(
      arrow::acero::ExecPlan& plan,
      arrow::acero::ExecNode* input_node,
      const std::map<std::string, const Assignment*>& assignment_by_name
   ) const;
};

}  // namespace rhydb::query_engine::operators
