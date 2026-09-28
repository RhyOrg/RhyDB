#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/builder.h>
#include <arrow/record_batch.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json_fwd.hpp>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/batched_bitmap_reader.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::exec_node {

class ExecBatchBuilder {
   std::map<schema::ColumnType, std::map<std::string, std::shared_ptr<arrow::ArrayBuilder>>>
      array_builders;
   std::vector<rhydb::schema::ColumnIdentifier> output_fields;

  public:
   explicit ExecBatchBuilder(std::vector<rhydb::schema::ColumnIdentifier> output_fields);

   template <storage::column::Column Column>
   std::map<std::string, ArrowBuilder<Column>*> getColumnTypeArrayBuilders() {
      std::map<std::string, ArrowBuilder<Column>*> result;
      for (auto& [builder_name, builder] : array_builders.at(Column::TYPE)) {
         result.emplace(builder_name, dynamic_cast<ArrowBuilder<Column>*>(builder.get()));
      }
      return result;
   }

   arrow::Status appendEntries(const storage::Table& table, const Bitmap& row_ids);

   arrow::Result<arrow::ExecBatch> finishBatch();
};

class TableScanGenerator {
   ExecBatchBuilder exec_batch_builder;

   Bitmap bitmap_filter;

   std::optional<BatchedBitmapReader> current_bitmap_reader;

   const std::shared_ptr<const storage::Table> table;

  public:
   TableScanGenerator(
      const std::vector<rhydb::schema::ColumnIdentifier>& columns,
      Bitmap bitmap_filter_,
      std::shared_ptr<const storage::Table> table,
      size_t batch_size_cutoff
   )
       : exec_batch_builder(columns),
         bitmap_filter(std::move(bitmap_filter_)),
         table(std::move(table)) {
      current_bitmap_reader = BatchedBitmapReader{bitmap_filter.toRoaring(), batch_size_cutoff};
   }

   arrow::Future<std::optional<arrow::ExecBatch>> operator()() {
      SPDLOG_TRACE("TableScanGenerator::operator()");
      auto future = arrow::Future<std::optional<arrow::ExecBatch>>::Make();
#ifdef __EMSCRIPTEN__
      // In the browser build we produce the batch synchronously. Spawning a
      // detached pthread per batch (as the native path does below) starves or
      // deadlocks Emscripten's fixed pthread worker pool (PTHREAD_POOL_SIZE in
      // wasm/CMakeLists.txt) on larger datasets. The arrow issues worked around
      // below do not affect the browser build, which runs acero with a
      // single-threaded executor.
      try {
         auto result = produceNextBatch();
         future.MarkFinished(std::move(result));
      } catch (const std::exception& exception) {
         future.MarkFinished(arrow::Status::ExecutionError(exception.what()));
      }
#else
      // We do this to guard against https://github.com/apache/arrow/issues/47641
      // and https://github.com/apache/arrow/issues/47642
      std::thread([future, this]() mutable {
         try {
            auto result = produceNextBatch();
            future.MarkFinished(std::move(result));
         } catch (const std::exception& exception) {
            future.MarkFinished(arrow::Status::ExecutionError(exception.what()));
         }
         ,
      }).detach();
#endif
      return future;
   };

  private:
   arrow::Result<std::optional<arrow::ExecBatch>> produceNextBatch();
};

arrow::Result<arrow::acero::ExecNode*> makeTableScan(
   arrow::acero::ExecPlan* plan,
   const std::vector<rhydb::schema::ColumnIdentifier>& columns,
   Bitmap bitmap_filter_,
   std::shared_ptr<const storage::Table> table,
   size_t batch_size_cutoff
);

}  // namespace rhydb::query_engine::exec_node
