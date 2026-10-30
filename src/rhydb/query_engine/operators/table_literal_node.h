#pragma once

#include <map>
#include <memory>
#include <vector>

#include <arrow/result.h>
#include <arrow/table.h>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

/// Source-type node that emits a fixed, inline table written in the query, e.g.
/// `table({{a:=1,b:=2},{3,4}})`. It takes no input; the column schema and the row
/// values are fixed when the node is built. Rows are emitted in the order written.
class TableLiteralNode final : public QueryNode {
  public:
   /// @param columns the output schema, in column order.
   /// @param table an Arrow table whose fields match `columns` (name and type).
   TableLiteralNode(
      std::vector<schema::ColumnIdentifier> columns,
      std::shared_ptr<arrow::Table> table
   );

   [[nodiscard]] std::vector<schema::ColumnIdentifier> getOutputSchema() const override;

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const override;

   [[nodiscard]] NodeKind kind() const override { return NodeKind::TABLE_LITERAL; }

   [[nodiscard]] nlohmann::json toJson() const override;

  private:
   std::vector<schema::ColumnIdentifier> columns_;
   std::shared_ptr<arrow::Table> table_;
};

}  // namespace rhydb::query_engine::operators
