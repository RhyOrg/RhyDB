#include "rhydb/query_engine/planner.h"

#include <stdexcept>

#include <arrow/acero/exec_plan.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/optimizer/bitmap_aggregation_rewrite_pass.h"
#include "rhydb/query_engine/optimizer/column_narrowing_pass.h"
#include "rhydb/query_engine/optimizer/filter_pushdown_pass.h"
#include "rhydb/query_engine/optimizer/map_pullup_pass.h"
#include "rhydb/query_engine/optimizer/node_resolution_pass.h"
#include "rhydb/query_engine/optimizer/select_k_rewrite_pass.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine {

namespace {

using optimizer::BitmapAggregationRewritePass;
using optimizer::ColumnNarrowingPass;
using optimizer::FilterPushdownPass;
using optimizer::MapPullupPass;
using optimizer::NodeResolutionPass;
using optimizer::SelectKRewritePass;

arrow::Result<QueryPlan> planQueryOrError(
   const operators::QueryNode& node,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   ARROW_ASSIGN_OR_RAISE(auto arrow_plan, arrow::acero::ExecPlan::Make());
   ARROW_ASSIGN_OR_RAISE(auto* top_node, node.addToExecPlan(*arrow_plan, tables, query_options));
   return QueryPlan::makeQueryPlan(std::move(arrow_plan), top_node, request_id);
}

}  // namespace

operators::QueryNodePtr Planner::optimize(
   operators::QueryNodePtr node,
   std::string_view request_id
) {
   auto log_plan = [&](std::string_view phase) {
      if (spdlog::should_log(spdlog::level::debug)) {
         SPDLOG_DEBUG("[{}] {}: {}", request_id, phase, node->toJson().dump());
      }
   };
   log_plan("initial");
   // FilterPushdownPass must run before ColumnNarrowingPass: it is the single owner of the
   // filter/map interaction. Running it first means a filter that cannot be pushed below a map
   // is left as a FilterNode above that map (with the producing assignments intact), while a
   // pushable filter is moved into the scan. ColumnNarrowingPass then prunes safely: it only
   // keeps a producing map alive when a non-eliminable FilterNode still references its output,
   // and still drops maps whose filters were pushed to the scan (preserving #1343).
   node = FilterPushdownPass::run(std::move(node));
   log_plan("after FilterPushdownPass");
   node = ColumnNarrowingPass::run(std::move(node));
   log_plan("after ColumnNarrowingPass");
   node = MapPullupPass::run(std::move(node));
   log_plan("after MapPullupPass");
   node = SelectKRewritePass::run(std::move(node));
   log_plan("after SelectKRewritePass");
   node = BitmapAggregationRewritePass::run(std::move(node));
   log_plan("after BitmapAggregationRewritePass");
   node = NodeResolutionPass::run(std::move(node));
   log_plan("after NodeResolutionPass");
   return node;
}

QueryPlan Planner::planQuery(
   operators::QueryNodePtr node,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   node = optimize(std::move(node), request_id);
   auto result = planQueryOrError(*node, tables, query_options, request_id);
   if (!result.ok()) {
      throw std::runtime_error(
         fmt::format("Error when planning query execution: {}", result.status().ToString())
      );
   }
   return std::move(result.ValueUnsafe());
}

QueryPlan Planner::planSaneqlQuery(
   std::string_view query_string,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   auto query_node = saneql::parseAndConvertToQueryTree(query_string, tables);
   return planQuery(std::move(query_node), tables, query_options, request_id);
}

}  // namespace rhydb::query_engine
