#include "rhydb/query_engine/operators/table_literal_node.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/compute/exec.h>
#include <arrow/table.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/async_batch_producer.h"
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
   // The table is emitted as a single batch, in the order its rows were written. An implicit
   // ordering keeps that order stable through the rest of the plan.
   exec_node::BatchProducer producer =
      [table = table_,
       already_produced = false] mutable -> arrow::Result<std::optional<arrow::ExecBatch>> {
      if (already_produced) {
         return std::nullopt;
      }
      already_produced = true;

      std::shared_ptr<arrow::Table> combined;
      ARROW_ASSIGN_OR_RAISE(combined, table->CombineChunks());
      std::vector<arrow::Datum> values;
      values.reserve(combined->num_columns());
      for (const auto& column : combined->columns()) {
         // A table() literal always has at least one row, so CombineChunks() leaves each column
         // with exactly one chunk.
         RHYDB_ASSERT_EQ(column->num_chunks(), 1);
         values.emplace_back(column->chunk(0));
      }
      return arrow::ExecBatch::Make(std::move(values), combined->num_rows());
   };

   const arrow::acero::SourceNodeOptions options{
      exec_node::columnsToArrowSchema(getOutputSchema()),
      exec_node::makeAsyncBatchGenerator(plan, std::move(producer)),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", &plan, {}, options);
}

nlohmann::json TableLiteralNode::toJson() const {
   return {
      {"type", nodeKindToString(kind())},
      {"columns", columnsToJson(columns_)},
   };
}

}  // namespace rhydb::query_engine::operators
