#include "rhydb/query_engine/query_plan.h"

#include <arrow/acero/query_context.h>
#include <arrow/array.h>
#include <arrow/compute/ordering.h>
#include <arrow/record_batch.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/common/size_constants.h"

namespace rhydb::query_engine {

std::string serializeResultOrdering(const arrow::compute::Ordering& ordering) {
   auto sort_keys = nlohmann::json::array();
   for (const auto& sort_key : ordering.sort_keys()) {
      const std::string* field_name = sort_key.target.name();
      sort_keys.push_back(
         {{"field", field_name != nullptr ? *field_name : sort_key.target.ToString()},
          {"order",
           sort_key.order == arrow::compute::SortOrder::Ascending ? "ascending" : "descending",},
          {"nullPlacement",
           sort_key.null_placement == arrow::compute::NullPlacement::AtStart ? "atStart" : "atEnd",},}
      );
   }
   return sort_keys.dump();
}

arrow::Result<QueryPlan> QueryPlan::makeQueryPlan(
   std::shared_ptr<arrow::acero::ExecPlan> arrow_plan,
   arrow::acero::ExecNode* root,
   std::string_view request_id
) {
   arrow::acero::BackpressureMonitor* backpressure_monitor;
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> results_generator;
   arrow::compute::Ordering result_ordering = root->ordering();
   ARROW_ASSIGN_OR_RAISE(
      backpressure_monitor, createGenerator(arrow_plan.get(), root, &results_generator)
   );
   QueryPlan query_plan{
      std::move(arrow_plan), std::move(results_generator), backpressure_monitor, request_id,
   };
   query_plan.results_schema = root->output_schema();
   query_plan.result_ordering = std::move(result_ordering);
   ARROW_RETURN_NOT_OK(query_plan.arrow_plan->Validate());
   return query_plan;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status QueryPlan::executeAndWriteImpl(
   exec_node::ArrowBatchSink& output_sink,
   uint64_t timeout_in_seconds
) {
   EVOBENCH_SCOPE("QueryPlan", "execute");
   SPDLOG_TRACE("{}", arrow_plan->ToString());
   SPDLOG_DEBUG("Request Id [{}] - QueryPlan - Starting the plan.", request_id);
   arrow_plan->StartProducing();
   SPDLOG_DEBUG(
      "Request Id [{}] - QueryPlan - Plan started producing, will now read the resulting batches.",
      request_id
   );

   // Ensure plan is stopped on any exit path (timeout/error/exception).
   const struct PlanStopGuard {
      std::string_view request_id;
      std::shared_ptr<arrow::acero::ExecPlan> plan;

      ~PlanStopGuard() {
         constexpr double GRACE_SHUTDOWN_SECONDS = 5.0;  // avoid hanging on teardown
         try {
            if (plan) {
               SPDLOG_DEBUG(
                  "Request Id [{}] - QueryPlan - Stopping arrow execution plan", request_id
               );
               auto finished_future = plan->finished();
               // Guard against the case, where the plan was not properly started, only call
               // StopProducing when the plan is still FutureState::PENDING (== not finished)
               if (!finished_future.is_finished()) {
                  plan->StopProducing();
                  const bool drained = finished_future.Wait(GRACE_SHUTDOWN_SECONDS);
                  if (!drained) {
                     SPDLOG_WARN(
                        "Request Id [{}] - QueryPlan - ExecPlan cleanup exceeded {} s grace; "
                        "continuing.",
                        request_id,
                        GRACE_SHUTDOWN_SECONDS
                     );
                  }
               }
            }
         } catch (const std::exception& e) {
            SPDLOG_ERROR(
               "Request Id [{}] - QueryPlan - Error while tearing down Arrow::acero::ExecPlan: {}",
               request_id,
               e.what()
            );
         } catch (...) {
            SPDLOG_ERROR(
               "Request Id [{}] - QueryPlan - Unknown non-std::exception error while tearing down "
               "Arrow::acero::ExecPlan.",
               request_id
            );
         }
      }
   } guard{.request_id = request_id, .plan = arrow_plan};

   while (true) {
      const arrow::Future<std::optional<arrow::ExecBatch>> future_batch = results_generator();
      SPDLOG_DEBUG("Request Id [{}] - QueryPlan - await the next batch", request_id);
      const bool finished_batch_in_time =
         future_batch.Wait(static_cast<double>(timeout_in_seconds));
      if (!finished_batch_in_time) {
         SPDLOG_WARN(
            "Request Id [{}] - QueryPlan - Batch wait timed out after {} s — stopping plan.",
            request_id,
            timeout_in_seconds
         );
         return arrow::Status::ExecutionError(
            fmt::format("Request timed out, no batch within {} seconds.", timeout_in_seconds)
         );
      }
      ARROW_ASSIGN_OR_RAISE(std::optional<arrow::ExecBatch> optional_batch, future_batch.result());
      SPDLOG_DEBUG("Request Id [{}] - QueryPlan - Batch received", request_id);
      SPDLOG_DEBUG(
         "Request Id [{}] - QueryPlan - Current backpressure size: {} bytes, operation is {}",
         request_id,
         backpressure_monitor->bytes_in_use(),
         backpressure_monitor->is_paused() ? "paused" : "running"
      );

      if (!optional_batch.has_value()) {
         break;  // end of input
      }
      SPDLOG_DEBUG(
         "Request Id [{}] - QueryPlan - Batch contains data with {} values.",
         request_id,
         optional_batch.value().length
      );

      ARROW_RETURN_NOT_OK(output_sink.writeBatch(optional_batch.value()));
   };
   ARROW_RETURN_NOT_OK(output_sink.finish());
   SPDLOG_DEBUG("Request Id [{}] - QueryPlan - Finished reading all batches.", request_id);
   return arrow::Status::OK();
}

void QueryPlan::executeAndWrite(
   exec_node::ArrowBatchSink& output_sink,
   uint64_t timeout_in_seconds
) {
   auto status = executeAndWriteImpl(output_sink, timeout_in_seconds);
   if (!status.ok()) {
      if (status.IsIOError()) {
         SPDLOG_WARN(
            "The request {} encountered an IO Error when sending the response. We expect that the "
            "user cancelled the request while the response was send and ignore the error",
            request_id
         );
      } else {
         throw std::runtime_error(fmt::format(
            "Request Id [{}] - Internal server error. Please notify developers. RhyDB likely "
            "constructed an invalid arrow plan and more user-input validation needs to be "
            "added: "
            "{}",
            request_id,
            status.message()
         ));
      }
   }
}

arrow::Result<arrow::acero::BackpressureMonitor*> QueryPlan::createGenerator(
   arrow::acero::ExecPlan* plan,
   arrow::acero::ExecNode* input,
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>>* generator
) {
   arrow::acero::BackpressureMonitor* backpressure_monitor;
   const arrow::acero::SinkNodeOptions options{
      generator,
      arrow::acero::BackpressureOptions{
         /*.resume_if_below =*/common::S_16_KB,
         /*.pause_if_above =*/common::S_64_MB,
      },
      &backpressure_monitor,
   };

   ARROW_ASSIGN_OR_RAISE(
      auto node, arrow::acero::MakeExecNode(std::string{"sink"}, plan, {input}, options)
   );
   node->SetLabel("final sink of the plan");
   return backpressure_monitor;
}

}  // namespace rhydb::query_engine
