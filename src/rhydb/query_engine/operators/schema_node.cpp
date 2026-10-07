#include "rhydb/query_engine/operators/schema_node.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/builder.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::operators {

namespace {

arrow::Result<arrow::ExecBatch> buildSchemaBatch(
   const std::vector<schema::ColumnIdentifier>& input_schema
) {
   arrow::StringBuilder field_name_builder{};
   arrow::StringBuilder type_builder{};
   for (const auto& field : input_schema) {
      ARROW_RETURN_NOT_OK(field_name_builder.Append(field.name));
      ARROW_RETURN_NOT_OK(type_builder.Append(schema::columnTypeToString(field.type)));
   }

   arrow::Datum field_name_datum;
   ARROW_ASSIGN_OR_RAISE(field_name_datum, field_name_builder.Finish());
   arrow::Datum type_datum;
   ARROW_ASSIGN_OR_RAISE(type_datum, type_builder.Finish());

   return arrow::ExecBatch::Make({field_name_datum, type_datum});
}

}  // namespace

SchemaNode::SchemaNode(QueryNodePtr child)
    : child(std::move(child)) {}

std::vector<schema::ColumnIdentifier> SchemaNode::getOutputSchema() const {
   return {
      {.name = std::string{FIELD_NAME_COLUMN}, .type = schema::ColumnType::STRING},
      {.name = std::string{TYPE_COLUMN}, .type = schema::ColumnType::STRING},
   };
}

arrow::Result<arrow::acero::ExecNode*> SchemaNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& /*query_options*/
) const {
   std::function<arrow::Future<std::optional<arrow::ExecBatch>>()> producer =
      [input_schema = child->getOutputSchema(),
       already_produced = false]() mutable -> arrow::Future<std::optional<arrow::ExecBatch>> {
      if (already_produced) {
         const std::optional<arrow::ExecBatch> result = std::nullopt;
         return arrow::Future{result};
      }
      already_produced = true;

      ARROW_ASSIGN_OR_RAISE(const arrow::ExecBatch batch, buildSchemaBatch(input_schema));
      const std::optional<arrow::ExecBatch> result = batch;
      return arrow::Future{result};
   };

   const arrow::acero::SourceNodeOptions options{
      exec_node::columnsToArrowSchema(getOutputSchema()),
      std::move(producer),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", &plan, {}, options);
}

nlohmann::json SchemaNode::toJson() const {
   return {
      {"type", nodeKindToString(kind())},
      {"child", child->toJson()},
   };
}

}  // namespace rhydb::query_engine::operators
