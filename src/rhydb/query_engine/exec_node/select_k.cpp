#include "rhydb/query_engine/exec_node/select_k.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/acero/query_context.h>
#include <arrow/acero/util.h>
#include <arrow/compute/api_vector.h>
#include <arrow/compute/exec.h>
#include <arrow/compute/ordering.h>
#include <arrow/datum.h>
#include <arrow/record_batch.h>
#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/table.h>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine::exec_node {

namespace {

constexpr std::string_view SELECT_K_FACTORY_NAME = "rhydb_select_k";

// Carries the sort order and output window from `addSelectKNode` into the node factory.
struct SelectKNodeOptions : public arrow::acero::ExecNodeOptions {
   SelectKNodeOptions(arrow::Ordering ordering, int64_t offset, int64_t limit)
       : ordering(std::move(ordering)),
         offset(offset),
         limit(limit) {}

   arrow::Ordering ordering;
   int64_t offset;
   int64_t limit;
};

// A select-k Acero node
// Closely following the implementation of the acero-native SelectK-node:
// https://github.com/apache/arrow/blob/397b3d0023030f6c6bc69d214ea7b27a687256f8/cpp/src/arrow/acero/order_by_impl.cc
class SelectK : public arrow::acero::ExecNode {
  public:
   SelectK(
      arrow::acero::ExecPlan* plan,
      std::vector<arrow::acero::ExecNode*> inputs,
      std::shared_ptr<arrow::Schema> output_schema,
      arrow::Ordering ordering,
      int64_t offset,
      int64_t limit
   )
       : ExecNode(plan, std::move(inputs), {"input"}, std::move(output_schema)),
         ordering_(std::move(ordering)),
         offset_(offset),
         limit_(limit),
         k_(offset + limit) {}

   static arrow::Result<arrow::acero::ExecNode*> make(
      arrow::acero::ExecPlan* plan,
      std::vector<arrow::acero::ExecNode*> inputs,
      const arrow::acero::ExecNodeOptions& options
   ) {
      if (inputs.size() != 1) {
         return arrow::Status::Invalid(
            "rhydb_select_k requires exactly one input, got ", inputs.size()
         );
      }
      const auto& select_k_options = static_cast<const SelectKNodeOptions&>(options);
      std::shared_ptr<arrow::Schema> output_schema = inputs[0]->output_schema();
      return plan->EmplaceNode<SelectK>(
         plan,
         std::move(inputs),
         std::move(output_schema),
         select_k_options.ordering,
         select_k_options.offset,
         select_k_options.limit
      );
   }

   [[nodiscard]] const char* kind_name() const override { return "SelectK"; }

   [[nodiscard]] const arrow::Ordering& ordering() const override { return ordering_; }

   arrow::Status StartProducing() override { return arrow::Status::OK(); }

   void PauseProducing(arrow::acero::ExecNode* /*output*/, int32_t counter) override {
      inputs_[0]->PauseProducing(this, counter);
   }

   void ResumeProducing(arrow::acero::ExecNode* /*output*/, int32_t counter) override {
      inputs_[0]->ResumeProducing(this, counter);
   }

   arrow::Status InputReceived(arrow::acero::ExecNode* /*input*/, arrow::ExecBatch batch) override {
      ARROW_ASSIGN_OR_RAISE(auto record_batch, batch.ToRecordBatch(output_schema_));
      {
         std::scoped_lock lock(mutex_);
         batches_.push_back(std::move(record_batch));
      }
      // Every batch is buffered before Increment(), so when the counter reports completion the
      // whole input is available to select from.
      if (counter_.Increment()) {
         return doFinish();
      }
      return arrow::Status::OK();
   }

   arrow::Status InputFinished(arrow::acero::ExecNode* /*input*/, int total_batches) override {
      // Like Arrow's OrderByNode we cannot forward InputFinished eagerly: cutting the window
      // changes the batch count, so downstream learns it only from doFinish().
      if (counter_.SetTotal(total_batches)) {
         return doFinish();
      }
      return arrow::Status::OK();
   }

  protected:
   arrow::Status StopProducingImpl() override { return arrow::Status::OK(); }

   [[nodiscard]] std::string ToStringExtra(int /*indent*/) const override {
      std::stringstream stream;
      stream << "ordering=" << ordering_.ToString() << " offset=" << offset_ << " limit=" << limit_;
      return stream.str();
   }

  private:
   // All input has been consumed: materialize it, run the heap-based top-k over the whole table,
   // cut the [offset, offset + limit) window
   arrow::Status doFinish() {
      std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
      {
         std::scoped_lock lock(mutex_);
         batches = std::move(batches_);
      }
      ARROW_ASSIGN_OR_RAISE(auto table, arrow::Table::FromRecordBatches(output_schema_, batches));

      arrow::compute::ExecContext* ctx = plan_->query_context()->exec_context();
      std::shared_ptr<arrow::Table> selected;
      // Manually check for num_rows == 0 until https://github.com/apache/arrow/issues/51210 is
      // fixed
      if (table->num_rows() == 0) {
         selected = std::move(table);
      } else {
         const arrow::compute::SelectKOptions options{k_, ordering_.sort_keys()};
         ARROW_ASSIGN_OR_RAISE(
            auto indices, arrow::compute::SelectKUnstable(arrow::Datum{table}, options, ctx)
         );
         ARROW_ASSIGN_OR_RAISE(
            auto taken,
            arrow::compute::Take(
               arrow::Datum{table},
               arrow::Datum{indices},
               arrow::compute::TakeOptions::NoBoundsCheck(),
               ctx
            )
         );
         selected = taken.table();
      }

      // select_k returns the smallest `offset + limit` rows in sorted order; drop the leading
      // `offset` of them to land on the [offset, offset + limit) window.
      const int64_t rows = selected->num_rows();
      const int64_t window_offset = std::min(offset_, rows);
      const int64_t window_length = std::min(limit_, rows - window_offset);
      auto window = selected->Slice(window_offset, window_length);

      arrow::TableBatchReader reader(*window);
      reader.set_chunksize(arrow::acero::ExecPlan::kMaxBatchSize);
      int batch_index = 0;
      while (true) {
         ARROW_ASSIGN_OR_RAISE(std::shared_ptr<arrow::RecordBatch> next, reader.Next());
         if (next == nullptr) {
            return output_->InputFinished(this, batch_index);
         }
         const int index = batch_index++;
         plan_->query_context()->ScheduleTask(
            [this, batch = std::move(next), index] -> arrow::Status {
               arrow::ExecBatch exec_batch(*batch);
               exec_batch.index = index;
               return output_->InputReceived(this, std::move(exec_batch));
            },
            "SelectK::ProcessBatch"
         );
      }
   }

   arrow::acero::AtomicCounter counter_;
   arrow::Ordering ordering_;
   int64_t offset_;
   int64_t limit_;
   int64_t k_;
   std::mutex mutex_;
   // Every input row, buffered until all input arrives (no pruning). Guarded by `mutex_`.
   std::vector<std::shared_ptr<arrow::RecordBatch>> batches_;
};

// Register the factory with Acero's default registry exactly once per process.
void registerSelectKFactory() {
   static std::once_flag registered;
   std::call_once(registered, [] {
      const arrow::Status status = arrow::acero::default_exec_factory_registry()->AddFactory(
         std::string{SELECT_K_FACTORY_NAME}, SelectK::make
      );
      RHYDB_ASSERT(status.ok());
   });
}

}  // namespace

arrow::Result<arrow::acero::ExecNode*> addSelectKNode(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* input_node,
   const arrow::Ordering& ordering,
   uint32_t offset,
   uint32_t limit
) {
   registerSelectKFactory();
   ARROW_ASSIGN_OR_RAISE(
      auto* node,
      arrow::acero::MakeExecNode(
         std::string{SELECT_K_FACTORY_NAME},
         &plan,
         {input_node},
         SelectKNodeOptions{ordering, static_cast<int64_t>(offset), static_cast<int64_t>(limit)}
      )
   );
   node->SetLabel("select_k with limit");
   return node;
}

}  // namespace rhydb::query_engine::exec_node
