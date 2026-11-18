#include "rhydb/query_engine/exec_node/stop_forwarding_node.h"

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/builder.h>
#include <arrow/compute/ordering.h>
#include <arrow/util/async_generator.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/query_plan.h"

using rhydb::query_engine::QueryPlan;
using rhydb::query_engine::exec_node::forwardStopToSink;
using rhydb::query_engine::exec_node::NdjsonSink;

namespace {

constexpr int ROWS_PER_BATCH = 100;
constexpr int BATCH_COUNT = 10000;

// A source of BATCH_COUNT batches, bridged through a sink that pauses its input as soon as one
// batch is queued (as the bridge in front of zstd decompression does), followed by a limit that is
// satisfied by the first batch.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<arrow::acero::ExecNode*> addBridgedSourceWithLimit(arrow::acero::ExecPlan& plan) {
   arrow::Int32Builder builder;
   for (int row = 0; row < ROWS_PER_BATCH; ++row) {
      ARROW_RETURN_NOT_OK(builder.Append(row));
   }
   ARROW_ASSIGN_OR_RAISE(auto column, builder.Finish());
   auto schema = arrow::schema({arrow::field("id", arrow::int32())});
   auto remaining_batches = std::make_shared<int>(BATCH_COUNT);
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> input =
      [remaining_batches, column] -> arrow::Future<std::optional<arrow::ExecBatch>> {
      if ((*remaining_batches)-- <= 0) {
         return arrow::Future<std::optional<arrow::ExecBatch>>::MakeFinished(std::nullopt);
      }
      return arrow::Future<std::optional<arrow::ExecBatch>>::MakeFinished(
         arrow::ExecBatch{{column}, column->length()}
      );
   };
   ARROW_ASSIGN_OR_RAISE(
      auto* node,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{schema, std::move(input), arrow::Ordering::Implicit()}
      )
   );

   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> bridged;
   arrow::acero::BackpressureMonitor* backpressure_monitor = nullptr;
   ARROW_ASSIGN_OR_RAISE(
      auto* bridge_sink,
      arrow::acero::MakeExecNode(
         "sink",
         &plan,
         {node},
         arrow::acero::SinkNodeOptions{
            &bridged,
            nullptr,
            arrow::acero::BackpressureOptions{/*resume_if_below=*/1, /*pause_if_above=*/1},
            &backpressure_monitor,
         }
      )
   );
   ARROW_ASSIGN_OR_RAISE(
      auto* bridge_source,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{schema, std::move(bridged), arrow::Ordering::Implicit()}
      )
   );
   ARROW_ASSIGN_OR_RAISE(node, forwardStopToSink(plan, bridge_source, bridge_sink));
   return arrow::acero::MakeExecNode(
      "fetch", &plan, {node}, arrow::acero::FetchNodeOptions{/*offset=*/0, /*count=*/10}
   );
}

std::chrono::milliseconds runToCompletion(std::string& result) {
   auto arrow_plan = arrow::acero::ExecPlan::Make().ValueOrDie();
   auto* root = addBridgedSourceWithLimit(*arrow_plan).ValueOrDie();
   auto query_plan = QueryPlan::makeQueryPlan(arrow_plan, root, "test").ValueOrDie();
   std::stringstream output;
   NdjsonSink output_sink{&output, query_plan.results_schema};

   const auto start = std::chrono::steady_clock::now();
   query_plan.executeAndWrite(output_sink, 10);
   const auto elapsed = std::chrono::steady_clock::now() - start;

   EXPECT_TRUE(arrow_plan->finished().is_finished());
   result = output.str();
   return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
}

}  // namespace

// The limit stops the bridge source once it has its rows. Without forwarding that stop to the
// bridge sink, the input source stays paused by the sink's backpressure, the plan cannot finish,
// and tearing it down waits out the full grace period (5 s) before stopping it.
TEST(StopForwardingNode, limitAfterBridgeFinishesWithoutWaitingForTeardownGrace) {
   std::string result;
   const auto elapsed = runToCompletion(result);

   EXPECT_LT(elapsed, std::chrono::seconds{2});
   EXPECT_EQ(result, R"({"id":0}
{"id":1}
{"id":2}
{"id":3}
{"id":4}
{"id":5}
{"id":6}
{"id":7}
{"id":8}
{"id":9}
)");
}
