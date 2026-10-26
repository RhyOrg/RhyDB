#pragma once

#include <utility>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/query_context.h>
#include <arrow/compute/ordering.h>
#include <arrow/util/async_generator_fwd.h>
#include <spdlog/spdlog.h>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine::exec_node {

using arrow::acero::BackpressureMonitor;

/// Source generator that slices the batches of `input_batches` into batches of at most
/// `batch_size` rows.
class BatchReslicer {
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> input_batches;
   int64_t batch_size;
   BackpressureMonitor* backpressure_monitor;

   std::optional<arrow::ExecBatch> current_batch;
   int64_t offset;
   int64_t remaining;  // always >0 when current_batch != std::nullopt

  public:
   BatchReslicer(
      arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> input_batches,
      int64_t batch_size,
      BackpressureMonitor* backpressure_monitor
   )
       : input_batches(std::move(input_batches)),
         batch_size(batch_size),
         backpressure_monitor(backpressure_monitor) {
      RHYDB_ASSERT(batch_size > 0);
   }

   // By arrow specification, this function will not be called re-entrantly
   arrow::Future<std::optional<arrow::compute::ExecBatch>> operator()();

  private:
   arrow::Result<std::optional<arrow::ExecBatch>> deliverSlicedBatch();
};

}  // namespace rhydb::query_engine::exec_node
