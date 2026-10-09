#include "rhydb/query_engine/exec_node/throttled_batch_reslicer.h"

namespace rhydb::query_engine::exec_node {

// By arrow specification, this function will not be called re-entrantly
arrow::Future<std::optional<arrow::compute::ExecBatch>> ThrottledBatchReslicer::operator()() {
   try {
      if (!current_batch.has_value()) {
         auto future = input_batches();
         return future.Then(
            [&](
               std::optional<arrow::ExecBatch> maybe_input_batch
            ) -> arrow::Result<std::optional<arrow::ExecBatch>> {
               SPDLOG_DEBUG(
                  "Current backpressure before BatchReslicer: {} with operation currently {}",
                  backpressure_monitor->bytes_in_use(),
                  backpressure_monitor->is_paused() ? "paused" : "running"
               );
               if (!maybe_input_batch.has_value()) {
                  return std::nullopt;
               }
               arrow::ExecBatch input_batch = std::move(maybe_input_batch).value();
               // If length is 0 we are supposed to emit an empty batch. We just return the input
               if (input_batch.length == 0) {
                  return input_batch;
               }
               current_batch = std::move(input_batch);
               offset = 0;
               remaining = current_batch.value().length;
               return deliverSlicedBatch();
            }
         );
      }
      return deliverSlicedBatch();
   } catch (const std::exception& exception) {
      SPDLOG_ERROR("Exception in BatchReslicer operator(): {}", exception.what());
      return arrow::Status::ExecutionError(exception.what());
   }
}

void ThrottledBatchReslicer::delayForTargetBatchRate() {
   auto now = std::chrono::system_clock::now();
   if (last_batch_delivered == std::nullopt) {
      last_batch_delivered = now;
      return;
   }
   auto time_elapsed = now - last_batch_delivered.value();
   if (time_elapsed < target_batch_rate) {
      std::this_thread::sleep_for(target_batch_rate - time_elapsed);
   }
   last_batch_delivered = std::chrono::system_clock::now();
}

arrow::Result<std::optional<arrow::ExecBatch>> ThrottledBatchReslicer::deliverSlicedBatch() {
   if (current_batch.value().length <= batch_size) {
      arrow::ExecBatch batch = std::move(current_batch.value());
      current_batch = std::nullopt;
      SPDLOG_DEBUG(
         "No reslicing necessary as ExecBatch size {} is not higher than desired batch size {}",
         batch.length,
         batch_size
      );
      return batch;
   }

   delayForTargetBatchRate();

   int64_t chunk_size = std::min(batch_size, remaining);
   arrow::ExecBatch batch = current_batch.value().Slice(offset, chunk_size);
   offset += chunk_size;
   remaining -= chunk_size;
   if (remaining == 0) {
      current_batch = std::nullopt;
   }
   SPDLOG_DEBUG("Emitting resliced batch of size {}", chunk_size);
   return batch;
}

}  // namespace rhydb::query_engine::exec_node
