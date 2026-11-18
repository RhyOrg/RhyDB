#include "rhydb/query_engine/operators/order_by_node.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/compute/api.h>
#include <arrow/compute/ordering.h>
#include <arrow/util/async_generator_fwd.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/stop_forwarding_node.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/order_by_randomize.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

namespace {

arrow::Result<arrow::acero::ExecNode*> addSortNode(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* top_node,
   const std::vector<schema::ColumnIdentifier>& output_fields,
   const arrow::Ordering& ordering
) {
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> generator;
   ARROW_ASSIGN_OR_RAISE(
      auto* sink_node,
      arrow::acero::MakeExecNode(
         "order_by_sink",
         &plan,
         {top_node},
         arrow::acero::OrderBySinkNodeOptions{arrow::SortOptions{ordering}, &generator}
      )
   );
   sink_node->SetLabel("order by");
   auto schema = exec_node::columnsToArrowSchema(output_fields);
   ARROW_ASSIGN_OR_RAISE(
      auto* source_node,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{schema, std::move(generator), ordering}
      )
   );
   return exec_node::forwardStopToSink(plan, source_node, sink_node);
}

}  // namespace

OrderByNode::OrderByNode(
   QueryNodePtr child,
   std::vector<OrderByField> fields,
   std::optional<uint32_t> randomize_seed
)
    : child(std::move(child)),
      fields(std::move(fields)),
      randomize_seed(randomize_seed) {}

std::vector<schema::ColumnIdentifier> OrderByNode::getOutputSchema() const {
   return child->getOutputSchema();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<arrow::acero::ExecNode*> OrderByNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options
) const {
   // Validate order-by fields exist in child output schema
   auto child_schema = child->getOutputSchema();
   std::vector<std::string> field_names;
   field_names.reserve(child_schema.size());
   for (const auto& identifier : child_schema) {
      field_names.push_back(identifier.name);
   }

   ARROW_ASSIGN_OR_RAISE(auto* top_node, child->addToExecPlan(plan, tables, query_options));

   using arrow::compute::NullPlacement;
   using arrow::compute::SortOrder;

   std::vector<arrow::compute::SortKey> sort_keys;
   for (const auto& order_by_field : fields) {
      auto sort_order = order_by_field.ascending ? SortOrder::Ascending : SortOrder::Descending;
      // order nulls as smallest element
      const auto null_placement =
         order_by_field.ascending ? NullPlacement::AtStart : NullPlacement::AtEnd;
      sort_keys.emplace_back(order_by_field.field.name, sort_order, null_placement);
   }
   if (randomize_seed.has_value()) {
      sort_keys.emplace_back(RANDOMIZE_HASH_FIELD_NAME);
   }

   if (sort_keys.empty()) {
      return top_node;
   }

   const arrow::Ordering ordering{sort_keys};

   if (randomize_seed.has_value()) {
      ARROW_ASSIGN_OR_RAISE(top_node, addRandomizeColumn(plan, top_node, randomize_seed.value()));
   }

   ARROW_ASSIGN_OR_RAISE(top_node, addSortNode(plan, top_node, getOutputSchema(), ordering));

   if (randomize_seed.has_value()) {
      ARROW_ASSIGN_OR_RAISE(top_node, removeRandomizeColumn(plan, top_node));
   }

   return top_node;
}

nlohmann::json OrderByNode::toJson() const {
   nlohmann::json fields_json = nlohmann::json::array();
   for (const auto& order_by_field : fields) {
      fields_json.push_back({
         {"field", columnToJson(order_by_field.field)},
         {"ascending", order_by_field.ascending},
      });
   }
   nlohmann::json result{
      {"type", nodeKindToString(kind())},
      {"fields", std::move(fields_json)},
      {"child", child->toJson()},
   };
   if (randomize_seed.has_value()) {
      result["randomizeSeed"] = randomize_seed.value();
   }
   return result;
}

}  // namespace rhydb::query_engine::operators
