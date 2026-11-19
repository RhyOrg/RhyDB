#include "rhydb/query_engine/operators/table_literal_node.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/table.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::operators {

TableLiteralNode::TableLiteralNode(
   std::vector<schema::ColumnIdentifier> columns,
   std::shared_ptr<arrow::Table> table
)
    : columns_(std::move(columns)),
      table_(std::move(table)) {}

std::vector<schema::ColumnIdentifier> TableLiteralNode::getOutputSchema() const {
   return columns_;
}

arrow::Result<arrow::acero::ExecNode*> TableLiteralNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& /*query_options*/
) const {
   const arrow::acero::TableSourceNodeOptions options{table_};
   return arrow::acero::MakeExecNode("table_source", &plan, {}, options);
}

nlohmann::json TableLiteralNode::toJson() const {
   return {
      {"type", nodeKindToString(kind())},
      {"columns", columnsToJson(columns_)},
   };
}

}  // namespace rhydb::query_engine::operators
