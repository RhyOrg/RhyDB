#include "rhydb/query_engine/operators/aggregate_node.h"

#include <memory>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/compute/api.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace {

using rhydb::query_engine::operators::AggregateDefinition;
using rhydb::query_engine::operators::AggregateFunction;

std::string arrowFunctionName(AggregateFunction func, bool has_groups) {
   switch (func) {
      case AggregateFunction::COUNT:
         return has_groups ? "hash_count_all" : "count_all";
      case AggregateFunction::SUM:
         return has_groups ? "hash_sum" : "sum";
   }
   RHYDB_UNREACHABLE();
}

arrow::acero::AggregateNodeOptions buildAggregateOptions(
   const std::vector<rhydb::schema::ColumnIdentifier>& group_by_fields,
   const std::vector<AggregateDefinition>& aggregates,
   const arrow::Schema& input_schema
) {
   const bool has_groups = !group_by_fields.empty();

   std::vector<arrow::compute::Aggregate> arrow_aggregates;
   arrow_aggregates.reserve(aggregates.size());

   for (const auto& agg : aggregates) {
      std::vector<arrow::FieldRef> source_refs;
      std::shared_ptr<arrow::compute::FunctionOptions> options;

      switch (agg.function) {
         case AggregateFunction::COUNT: {
            // TODO(#1231) implement path including source column
            CHECK_RHYDB_QUERY(
               !agg.source_column.has_value(), "count(<column_ref>) not yet implemented"
            );
            options = std::make_shared<arrow::compute::CountOptions>(
               arrow::compute::CountOptions::CountMode::ALL
            );
            break;
         }
         case AggregateFunction::SUM: {
            RHYDB_ASSERT(agg.source_column.has_value());
            source_refs.emplace_back(agg.source_column->name);
            options = std::make_shared<arrow::compute::ScalarAggregateOptions>();
            break;
         }
      }

      arrow_aggregates.emplace_back(
         arrowFunctionName(agg.function, has_groups),
         options,
         std::move(source_refs),
         agg.output_name
      );
   }

   if (!has_groups) {
      return arrow::acero::AggregateNodeOptions(std::move(arrow_aggregates));
   }

   std::vector<arrow::FieldRef> field_refs;
   field_refs.reserve(group_by_fields.size());
   for (const auto& field : group_by_fields) {
      RHYDB_ASSERT(input_schema.CanReferenceFieldByName(field.name).ok());
      field_refs.emplace_back(field.name);
   }

   return arrow::acero::AggregateNodeOptions(std::move(arrow_aggregates), std::move(field_refs));
}

using rhydb::schema::ColumnType;
ColumnType getType(const AggregateDefinition& aggregate_definition) {
   switch (aggregate_definition.function) {
      case AggregateFunction::COUNT:
         return ColumnType::INT64;
      case AggregateFunction::SUM:
         // Arrow widens integer sums to int64 and keeps floating point sums as double
         RHYDB_ASSERT(aggregate_definition.source_column.has_value());
         switch (aggregate_definition.source_column->type) {
            case ColumnType::INT32:
            case ColumnType::INT64:
               return ColumnType::INT64;
            case ColumnType::FLOAT:
               return ColumnType::FLOAT;
            default:
               throw rhydb::query_engine::IllegalQueryException(
                  "sum is only supported on numeric columns, not on '{}'",
                  aggregate_definition.source_column->name
               );
         }
   }
   RHYDB_UNREACHABLE();
}

}  // namespace

namespace rhydb::query_engine::operators {

std::string_view displayName(AggregateFunction aggregate) {
   switch (aggregate) {
      case AggregateFunction::COUNT:
         return "COUNT";
      case AggregateFunction::SUM:
         return "SUM";
   }
   RHYDB_UNREACHABLE();
}

AggregateNode::AggregateNode(
   QueryNodePtr child,
   std::vector<schema::ColumnIdentifier> group_by_fields,
   std::vector<AggregateDefinition> aggregates
)
    : child(std::move(child)),
      group_by_fields(std::move(group_by_fields)),
      aggregates(std::move(aggregates)) {}

std::vector<schema::ColumnIdentifier> AggregateNode::getOutputSchema() const {
   auto output_fields = group_by_fields;
   for (const auto& agg : aggregates) {
      output_fields.emplace_back(agg.output_name, getType(agg));
   }
   return output_fields;
}

arrow::Result<arrow::acero::ExecNode*> AggregateNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options
) const {
   ARROW_ASSIGN_OR_RAISE(auto* child_node, child->addToExecPlan(plan, tables, query_options));

   auto input_schema = child_node->output_schema();

   const arrow::acero::AggregateNodeOptions aggregate_node_options =
      buildAggregateOptions(group_by_fields, aggregates, *input_schema);

   return arrow::acero::MakeExecNode("aggregate", &plan, {child_node}, aggregate_node_options);
}

nlohmann::json AggregateNode::toJson() const {
   nlohmann::json aggregates_json = nlohmann::json::array();
   for (const auto& agg : aggregates) {
      nlohmann::json agg_json{
         {"outputName", agg.output_name},
         {"function", displayName(agg.function)},
      };
      if (agg.source_column.has_value()) {
         agg_json["sourceColumn"] = columnToJson(agg.source_column.value());
      }
      aggregates_json.push_back(std::move(agg_json));
   }
   return {
      {"type", nodeKindToString(kind())},
      {"groupByFields", columnsToJson(group_by_fields)},
      {"aggregates", std::move(aggregates_json)},
      {"child", child->toJson()},
   };
}

}  // namespace rhydb::query_engine::operators
