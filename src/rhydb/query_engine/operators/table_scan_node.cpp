#include "rhydb/query_engine/operators/table_scan_node.h"

#include <nlohmann/json.hpp>

#include "rhydb/query_engine/exec_node/table_scan.h"
#include "rhydb/query_engine/operators/compute_filter.h"

namespace rhydb::query_engine::operators {

TableScanNode::TableScanNode(
   std::shared_ptr<storage::Table> table,
   std::unique_ptr<scalar_expressions::ScalarExpression> filter,
   std::vector<schema::ColumnIdentifier> fields
)
    : table(std::move(table)),
      filter(std::move(filter)),
      fields(std::move(fields)) {}

std::vector<schema::ColumnIdentifier> TableScanNode::getOutputSchema() const {
   return fields;
}

arrow::Result<arrow::acero::ExecNode*> TableScanNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& query_options
) const {
   auto bitmap_filter = compileFilter(filter, table)->evaluate();

   return exec_node::makeTableScan(
      &plan, fields, std::move(bitmap_filter), table, query_options.materialization_cutoff
   );
}

nlohmann::json TableScanNode::toJson() const {
   return {
      {"type", nodeKindToString(kind())},
      {"table", table->logTable()},
      {"filter", filter->toString()},
      {"fields", columnsToJson(fields)},
   };
}

}  // namespace rhydb::query_engine::operators
