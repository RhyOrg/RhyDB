#include "rhydb/query_engine/operators/order_by_randomize.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/builder.h>
#include <arrow/compute/api.h>
#include <arrow/util/async_generator_fwd.h>
#include <spdlog/spdlog.h>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine::operators {

const std::string RANDOMIZE_HASH_FIELD_NAME{"__RHYDB_RANDOMIZE_HASH"};

namespace {

uint64_t hash64(uint64_t value, uint64_t seed) {
   value ^= seed;
   value ^= value >> 33;
   value *= 0xff51afd7ed558ccdULL;
   value ^= value >> 33;
   value *= 0xc4ceb9fe1a85ec53ULL;
   value ^= value >> 33;
   return value;
}

}  // namespace

arrow::Result<arrow::acero::ExecNode*> removeRandomizeColumn(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* top_node
) {
   std::vector<arrow::Expression> field_refs;
   for (const auto& field : top_node->output_schema()->fields()) {
      if (field->name() != RANDOMIZE_HASH_FIELD_NAME) {
         field_refs.push_back(arrow::compute::field_ref(field->name()));
      }
   }
   auto options = arrow::acero::ProjectNodeOptions(field_refs);
   return arrow::acero::MakeExecNode("project", &plan, {top_node}, options);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<arrow::acero::ExecNode*> addRandomizeColumn(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* top_node,
   size_t randomize_seed
) {
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> sequenced_batches;
   std::shared_ptr<arrow::Schema> schema_of_sequence_batches;
   ARROW_ASSIGN_OR_RAISE(
      top_node,
      arrow::acero::MakeExecNode(
         "sink",
         &plan,
         {top_node},
         arrow::acero::SinkNodeOptions{&sequenced_batches, &schema_of_sequence_batches}
      )
   );
   top_node->SetLabel("input to randomize column projection");
   auto output_schema_fields = schema_of_sequence_batches->fields();
   output_schema_fields.emplace_back(
      std::make_shared<arrow::Field>(RANDOMIZE_HASH_FIELD_NAME, arrow::uint64())
   );
   auto output_schema = arrow::schema(output_schema_fields);
   size_t start_of_batch = 0;
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> sequenced_batches_with_hash_id =
      // NOLINTNEXTLINE(readability-function-cognitive-complexity)
      [sequenced_batches,
       start_of_batch,
       randomize_seed] mutable -> arrow::Future<std::optional<arrow::ExecBatch>> {
      SPDLOG_TRACE("randomize column projection awaits the next batch");
      auto future = sequenced_batches();

      return future.Then(
         [&](
            std::optional<arrow::ExecBatch> maybe_input_batch
         ) mutable -> arrow::Result<std::optional<arrow::ExecBatch>> {
            SPDLOG_TRACE("randomize column projection received next batch");

            if (!maybe_input_batch.has_value()) {
               return std::nullopt;
            }

            const auto& input_batch = maybe_input_batch.value();
            RHYDB_ASSERT(!input_batch.values.empty());
            auto rows_in_batch = input_batch.values.at(0).length();
            RHYDB_ASSERT_NE(rows_in_batch, arrow::Datum::kUnknownLength);

            arrow::UInt64Builder randomize_column_builder;
            for (int64_t i = 0; i < rows_in_batch; ++i) {
               const uint64_t hash_val = hash64(start_of_batch + i, randomize_seed);
               ARROW_RETURN_NOT_OK(randomize_column_builder.Append(hash_val));
            }

            ARROW_ASSIGN_OR_RAISE(auto randomize_column, randomize_column_builder.Finish());
            start_of_batch += rows_in_batch;

            auto output_columns = input_batch.values;
            output_columns.emplace_back(randomize_column);
            auto output_batch = arrow::ExecBatch::Make(output_columns);
            return output_batch;
         }
      );
   };
   ARROW_ASSIGN_OR_RAISE(
      top_node,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{output_schema, std::move(sequenced_batches_with_hash_id)}
      )
   );
   top_node->SetLabel("output of randomize column projection");
   return top_node;
}

}  // namespace rhydb::query_engine::operators
