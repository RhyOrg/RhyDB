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
#include "rhydb/query_engine/exec_node/async_batch_producer.h"
#include "rhydb/query_engine/operators/compute_filter.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::exec_node {

class ExecBatchBuilder {
   std::map<
      schema::DataTypePtr,
      std::map<std::string, std::shared_ptr<arrow::ArrayBuilder>>,
      schema::DataTypePtrLess>
      array_builders;
   std::vector<rhydb::schema::ColumnIdentifier> output_fields;

  public:
   explicit ExecBatchBuilder(std::vector<rhydb::schema::ColumnIdentifier> output_fields);

   template <storage::column::Column Column>
   std::map<std::string, ArrowBuilder<Column>*> getColumnTypeArrayBuilders() {
      std::map<std::string, ArrowBuilder<Column>*> result;
      for (auto& [type, builders] : array_builders) {
         if (!storage::column::storesType<Column>(*type)) {
            continue;
         }
         for (auto& [builder_name, builder] : builders) {
            result.emplace(builder_name, dynamic_cast<ArrowBuilder<Column>*>(builder.get()));
         }
      }
      return result;
   }

   arrow::Status appendEntries(const storage::Table& table, const Bitmap& row_ids);

   arrow::Result<arrow::ExecBatch> finishBatch(int64_t length);
};

class TableScanGenerator {
   ExecBatchBuilder exec_batch_builder;

   // Evaluated on the first batch, so that the filter is computed during the execution of the
   // plan and not while the plan is built
   std::shared_ptr<const operators::CompiledFilter> filter;

   size_t batch_size_cutoff;

   bool filter_computed = false;

   std::optional<BatchedBitmapReader> current_bitmap_reader;

   const std::shared_ptr<const storage::Table> table;

  public:
   TableScanGenerator(
      const std::vector<rhydb::schema::ColumnIdentifier>& columns,
      std::shared_ptr<const operators::CompiledFilter> filter,
      std::shared_ptr<const storage::Table> table,
      size_t batch_size_cutoff
   )
       : exec_batch_builder(columns),
         filter(std::move(filter)),
         batch_size_cutoff(batch_size_cutoff),
         table(std::move(table)) {}

   // By arrow specification, this function will not be called re-entrantly
   arrow::Result<std::optional<arrow::ExecBatch>> operator()();
};

arrow::Result<arrow::acero::ExecNode*> makeTableScan(
   arrow::acero::ExecPlan* plan,
   const std::vector<rhydb::schema::ColumnIdentifier>& columns,
   std::shared_ptr<const operators::CompiledFilter> filter,
   std::shared_ptr<const storage::Table> table,
   size_t batch_size_cutoff
);

}  // namespace rhydb::query_engine::exec_node
