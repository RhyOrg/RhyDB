#include "rhydb/query_engine/operators/fetch_node.h"

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/compute/ordering.h>
#include <arrow/util/async_generator_fwd.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/exec_node/stop_forwarding_node.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

namespace {

// Re-emits the batches of a node through a sink/source pair, with the resulting source declaring
// the given ordering. Used to attach a fabricated ordering to (and later strip it back off) an
// otherwise unordered result so that arrow's fetch node accepts it.
arrow::Result<arrow::acero::ExecNode*> resequenceWithOrdering(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* child_node,
   arrow::Ordering ordering,
   const std::string& label
) {
   auto schema = child_node->output_schema();
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> generator;
   ARROW_ASSIGN_OR_RAISE(
      auto* sink_node,
      arrow::acero::MakeExecNode(
         "sink", &plan, {child_node}, arrow::acero::SinkNodeOptions{&generator}
      )
   );
   sink_node->SetLabel(label);
   ARROW_ASSIGN_OR_RAISE(
      auto* source_node,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{
            std::move(schema), std::move(generator), std::move(ordering)
         }
      )
   );
   return exec_node::forwardStopToSink(plan, source_node, sink_node);
}

}  // namespace

FetchNode::FetchNode(
   QueryNodePtr child,
   std::optional<uint32_t> count,
   std::optional<uint32_t> offset
)
    : child(std::move(child)),
      count(count),
      offset(offset) {}

std::vector<schema::ColumnIdentifier> FetchNode::getOutputSchema() const {
   return child->getOutputSchema();
}

arrow::Result<arrow::acero::ExecNode*> FetchNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options
) const {
   ARROW_ASSIGN_OR_RAISE(auto* child_node, child->addToExecPlan(plan, tables, query_options));

   // arrow's fetch node requires an ordered input so that limit/offset is well-defined. If the
   // child produces an unordered result (e.g. an aggregation/group-by), the user is nonetheless
   // allowed to request a limit: we mark the result as implicitly ordered, accepting that the
   // retained rows are an arbitrary subset.
   const bool child_unordered = child_node->ordering().is_unordered();
   if (child_unordered) {
      ARROW_ASSIGN_OR_RAISE(
         child_node,
         resequenceWithOrdering(
            plan, child_node, arrow::Ordering::Implicit(), "resequence for limit/offset"
         )
      );
   }

   const arrow::acero::FetchNodeOptions fetch_options(
      offset.value_or(0), count.value_or(std::numeric_limits<int64_t>::max())
   );
   ARROW_ASSIGN_OR_RAISE(
      auto* fetch_node,
      arrow::acero::MakeExecNode(
         std::string{arrow::acero::FetchNodeOptions::kName}, &plan, {child_node}, fetch_options
      )
   );

   // The implicit ordering above was fabricated purely to satisfy the fetch node; the retained
   // rows are an arbitrary subset, so restore the unordered contract for downstream nodes.
   if (child_unordered) {
      return resequenceWithOrdering(
         plan, fetch_node, arrow::Ordering::Unordered(), "unordered result of limit/offset"
      );
   }
   return fetch_node;
}

nlohmann::json FetchNode::toJson() const {
   nlohmann::json result{
      {"type", nodeKindToString(kind())},
      {"child", child->toJson()},
   };
   if (count.has_value()) {
      result["count"] = count.value();
   }
   if (offset.has_value()) {
      result["offset"] = offset.value();
   }
   return result;
}

}  // namespace rhydb::query_engine::operators
