#include "rhydb/query_engine/exec_node/stop_forwarding_node.h"

#include <cstdint>
#include <utility>

#include <arrow/acero/exec_plan.h>
#include <arrow/compute/exec.h>
#include <arrow/compute/ordering.h>
#include <arrow/status.h>

namespace rhydb::query_engine::exec_node {

namespace {

/// Passes batches, backpressure and ordering through unchanged; only stopping it differs from
/// having no node at all. See `forwardStopToSink`.
class StopForwardingNode final : public arrow::acero::ExecNode {
   arrow::acero::ExecNode* bridge_sink;

  public:
   StopForwardingNode(
      arrow::acero::ExecPlan* plan,
      arrow::acero::ExecNode* bridge_source,
      arrow::acero::ExecNode* bridge_sink
   )
       : arrow::acero::ExecNode(
            plan,
            {bridge_source},
            {"bridge source"},
            bridge_source->output_schema()
         ),
         bridge_sink(bridge_sink) {}

   [[nodiscard]] const char* kind_name() const override { return "StopForwardingNode"; }

   [[nodiscard]] const arrow::compute::Ordering& ordering() const override {
      return inputs_[0]->ordering();
   }

   arrow::Status InputReceived(arrow::acero::ExecNode* /*input*/, arrow::ExecBatch batch) override {
      return output_->InputReceived(this, std::move(batch));
   }

   arrow::Status InputFinished(arrow::acero::ExecNode* /*input*/, int total_batches) override {
      return output_->InputFinished(this, total_batches);
   }

   arrow::Status StartProducing() override { return arrow::Status::OK(); }

   void PauseProducing(arrow::acero::ExecNode* /*output*/, int32_t counter) override {
      inputs_[0]->PauseProducing(this, counter);
   }

   void ResumeProducing(arrow::acero::ExecNode* /*output*/, int32_t counter) override {
      inputs_[0]->ResumeProducing(this, counter);
   }

  protected:
   // `ExecNode::StopProducing` stops the bridge source (our input) after this. Stopping the sink
   // closes its generator and propagates the stop to the nodes upstream of the bridge.
   // `StopProducing` is idempotent, so the plan stopping the sink again at teardown is harmless.
   arrow::Status StopProducingImpl() override { return bridge_sink->StopProducing(); }
};

}  // namespace

arrow::Result<arrow::acero::ExecNode*> forwardStopToSink(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* bridge_source,
   arrow::acero::ExecNode* bridge_sink
) {
   auto* node = plan.EmplaceNode<StopForwardingNode>(&plan, bridge_source, bridge_sink);
   node->SetLabel("forward stop of " + bridge_source->label() + " to " + bridge_sink->label());
   return node;
}

}  // namespace rhydb::query_engine::exec_node
