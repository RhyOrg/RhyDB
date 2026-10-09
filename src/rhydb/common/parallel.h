#include <concepts>
#include <exception>
#include <optional>

#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/util/future.h>
#include <arrow/util/macros.h>
#include <arrow/util/thread_pool.h>

#include "panic.h"

namespace rhydb::common {

class BlockedRange {
   size_t begin_;
   size_t end_;

  public:
   BlockedRange(size_t begin, size_t end)
       : begin_(begin),
         end_(end) {
      RHYDB_ASSERT_LE(begin, end);
   }
   [[nodiscard]] size_t begin() const { return begin_; }
   [[nodiscard]] size_t end() const { return end_; }
   [[nodiscard]] size_t size() const { return end_ - begin_; }
};

void parallelFor(
   BlockedRange range,
   size_t positions_per_process,
   std::invocable<BlockedRange> auto&& func
) {
   size_t num_chunks = range.size() / positions_per_process;
   if (range.size() % positions_per_process) {
      num_chunks++;
   }

   auto* pool = arrow::internal::GetCpuThreadPool();
   std::vector<arrow::Future<std::optional<std::exception_ptr>>> futures;
   for (size_t chunk = 0; chunk < num_chunks; chunk++) {
      const size_t last_chunk = num_chunks - 1;
      auto fut = pool
                    ->Submit(
                       [chunk, last_chunk, range, positions_per_process, &func]()
                          -> std::optional<std::exception_ptr> {
                          const size_t pos_begin = range.begin() + (chunk * positions_per_process);
                          size_t pos_end;
                          if (chunk < last_chunk) {
                             pos_end = pos_begin + positions_per_process;
                          } else {
                             pos_end = range.end();
                          }

                          try {
                             func(BlockedRange{pos_begin, pos_end});
                          } catch (...) {
                             return {std::current_exception()};
                          }
                          return {};
                       }
                    )
                    .ValueOrDie();
      futures.push_back(fut);
   }
   for (arrow::Future<std::optional<std::exception_ptr>>& future : futures) {
      arrow::Result<std::optional<std::exception_ptr>>&& result = future.MoveResult();
      std::optional<std::exception_ptr> perhaps_exception = result.ValueOrDie();
      if (perhaps_exception.has_value()) {
         std::rethrow_exception(perhaps_exception.value());
      }
   }
}

}  // namespace rhydb::common
