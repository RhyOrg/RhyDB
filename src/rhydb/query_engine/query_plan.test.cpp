#include "rhydb/query_engine/query_plan.h"

#include <atomic>
#include <thread>
#include <vector>

#include <arrow/compute/api_aggregate.h>
#include <arrow/compute/expression.h>
#include <arrow/compute/ordering.h>
#include <arrow/table.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "arrow/acero/options.h"
#include "arrow/builder.h"

#include "rhydb/query_engine/exec_node/ndjson_sink.h"

using rhydb::query_engine::QueryPlan;
using rhydb::query_engine::serializeResultOrdering;
using rhydb::query_engine::exec_node::NdjsonSink;

namespace {

arrow::Result<std::shared_ptr<arrow::Table>> setupTestTable() {
   const std::shared_ptr<arrow::Schema> schema =
      arrow::schema({arrow::field("id", arrow::int32())});

   arrow::Int32Builder id_builder;
   for (size_t id = 1; id <= 100000; ++id) {
      ARROW_RETURN_NOT_OK(id_builder.Append(id++));
   }
   ARROW_ASSIGN_OR_RAISE(const std::shared_ptr<arrow::Array> id_array, id_builder.Finish());
   return arrow::Table::Make(schema, {id_array});
}
}  // namespace

TEST(QueryPlan, reportsUnorderedForAggregatedResult) {
   auto arrow_plan = arrow::acero::ExecPlan::Make().ValueOrDie();
   auto table = setupTestTable().ValueOrDie();
   auto* node = arrow::acero::MakeExecNode(
                   "table_source", arrow_plan.get(), {}, arrow::acero::TableSourceNodeOptions{table}
   )
                   .ValueOrDie();

   auto count_options =
      std::make_shared<arrow::compute::CountOptions>(arrow::compute::CountOptions::CountMode::ALL);
   const arrow::compute::Aggregate aggregate{
      "hash_count_all", count_options, std::vector<arrow::FieldRef>{}, "count"
   };
   const arrow::acero::AggregateNodeOptions aggregate_node_options(
      {aggregate}, {arrow::FieldRef{"id"}}
   );
   node = arrow::acero::MakeExecNode("aggregate", arrow_plan.get(), {node}, aggregate_node_options)
             .ValueOrDie();

   auto under_test = QueryPlan::makeQueryPlan(arrow_plan, node, "some_id").ValueOrDie();

   EXPECT_EQ(serializeResultOrdering(under_test.result_ordering), "[]");
}

TEST(QueryPlan, reportsExplicitSortFieldsForOrderedResult) {
   auto arrow_plan = arrow::acero::ExecPlan::Make().ValueOrDie();
   auto table = setupTestTable().ValueOrDie();
   auto* node = arrow::acero::MakeExecNode(
                   "table_source", arrow_plan.get(), {}, arrow::acero::TableSourceNodeOptions{table}
   )
                   .ValueOrDie();

   const arrow::compute::Ordering ordering{
      {arrow::compute::SortKey{"id", arrow::compute::SortOrder::Descending}}
   };
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> generator;
   node = arrow::acero::MakeExecNode(
             "order_by_sink",
             arrow_plan.get(),
             {node},
             arrow::acero::OrderBySinkNodeOptions{arrow::SortOptions{ordering}, &generator}
   )
             .ValueOrDie();
   auto schema = arrow::schema({arrow::field("id", arrow::int32())});
   node = arrow::acero::MakeExecNode(
             "source",
             arrow_plan.get(),
             {},
             arrow::acero::SourceNodeOptions{schema, std::move(generator), ordering}
   )
             .ValueOrDie();

   auto under_test = QueryPlan::makeQueryPlan(arrow_plan, node, "some_id").ValueOrDie();

   EXPECT_EQ(
      nlohmann::json::parse(serializeResultOrdering(under_test.result_ordering)),
      nlohmann::json::array({{{"field", "id"}, {"order", "descending"}, {"nullPlacement", "atEnd"}}}
      )
   );
}

TEST(QueryPlan, timesOutWhenAnInvalidPlanDoesNotFinish) {
   EXPECT_THAT(
      ([]() {
         auto arrow_plan = arrow::acero::ExecPlan::Make().ValueOrDie();
         auto table = setupTestTable().ValueOrDie();
         auto node =
            arrow::acero::MakeExecNode(
               "table_source", arrow_plan.get(), {}, arrow::acero::TableSourceNodeOptions{table}
            )
               .ValueOrDie();

         auto count_options = std::make_shared<arrow::compute::CountOptions>(
            arrow::compute::CountOptions::CountMode::ALL
         );
         const arrow::compute::Aggregate aggregate{
            "hash_count_all", count_options, std::vector<arrow::FieldRef>{}, "count"
         };
         const arrow::acero::AggregateNodeOptions aggregate_node_options(
            {aggregate}, {arrow::FieldRef{"id"}}
         );
         node = arrow::acero::MakeExecNode(
                   "aggregate", arrow_plan.get(), {node}, aggregate_node_options
         )
                   .ValueOrDie();

         auto under_test = QueryPlan::makeQueryPlan(arrow_plan, node, "some_id").ValueOrDie();

         std::stringstream dummy_output{};
         NdjsonSink output_sink{&dummy_output, under_test.results_schema};
         // Set time-out to zero, so it immediately cancels execution (only works with pipeline
         // breakers like aggregate, because otherwise the StartProducing call might already do all
         // the work)
         under_test.executeAndWrite(output_sink, 0);
      }),
      ThrowsMessage<std::runtime_error>(::testing::HasSubstr(
         "Internal server error. Please notify developers. RhyDB likely constructed an invalid "
         "arrow plan and more user-input validation needs to be added: Request timed out, no batch"
         " within 0 seconds."
      ))
   );
}

// An empty grouped aggregate ends the stream before its plan finishes; don't abort it (#1035)
TEST(QueryPlan, doesNotAbortPlanAfterEmptyGroupedAggregateCompletes) {
   auto table = setupTestTable().ValueOrDie();
   std::atomic<int> aborted_plans = 0;
   constexpr int THREAD_COUNT = 16;
   constexpr int RUNS_PER_THREAD = 50;
   std::vector<std::thread> threads;
   threads.reserve(THREAD_COUNT);
   for (int thread = 0; thread < THREAD_COUNT; ++thread) {
      threads.emplace_back([&] {
         for (int run = 0; run < RUNS_PER_THREAD; ++run) {
            auto arrow_plan = arrow::acero::ExecPlan::Make().ValueOrDie();
            auto* node =
               arrow::acero::MakeExecNode(
                  "table_source", arrow_plan.get(), {}, arrow::acero::TableSourceNodeOptions{table}
               )
                  .ValueOrDie();
            node = arrow::acero::MakeExecNode(
                      "filter",
                      arrow_plan.get(),
                      {node},
                      arrow::acero::FilterNodeOptions{arrow::compute::less(
                         arrow::compute::field_ref("id"), arrow::compute::literal(0)
                      )}
            )
                      .ValueOrDie();
            auto count_options = std::make_shared<arrow::compute::CountOptions>(
               arrow::compute::CountOptions::CountMode::ALL
            );
            const arrow::compute::Aggregate aggregate{
               "hash_count_all", count_options, std::vector<arrow::FieldRef>{}, "count"
            };
            node = arrow::acero::MakeExecNode(
                      "aggregate",
                      arrow_plan.get(),
                      {node},
                      arrow::acero::AggregateNodeOptions{{aggregate}, {arrow::FieldRef{"id"}}}
            ).ValueOrDie();

            auto under_test = QueryPlan::makeQueryPlan(arrow_plan, node, "some_id").ValueOrDie();
            std::stringstream output{};
            NdjsonSink output_sink{&output, under_test.results_schema};
            under_test.executeAndWrite(output_sink, 10);
            if (!arrow_plan->finished().is_finished() || !arrow_plan->finished().status().ok()) {
               ++aborted_plans;
            }
         }
      });
   }
   for (auto& thread : threads) {
      thread.join();
   }
   EXPECT_EQ(aborted_plans, 0);
}
