#pragma once

#include <arrow/acero/exec_plan.h>
#include <arrow/result.h>

namespace rhydb::query_engine::exec_node {

/// Connects the two halves of a sink -> generator -> source bridge for stopping.
///
/// Several operators route batches through a sink node whose generator feeds a new source node
/// (to resequence, sort, reslice or materialize them). Arrow does not know that the two halves
/// belong together: `StopProducing()` travels from a node to its inputs, and the bridge's source
/// has none, so a stop from downstream (e.g. a fetch node that has emitted its limit) ends at the
/// source. The sink and everything upstream of it keep running. Once the sink applies
/// backpressure and nobody pulls from its generator anymore, they never finish, and the plan can
/// only be torn down by stopping it as a whole.
///
/// Returns a pass-through node placed directly after `bridge_source` that, when stopped, also
/// stops `bridge_sink` (and with it the nodes upstream of the bridge). Downstream nodes must be
/// attached to the returned node instead of `bridge_source`.
arrow::Result<arrow::acero::ExecNode*> forwardStopToSink(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* bridge_source,
   arrow::acero::ExecNode* bridge_sink
);

}  // namespace rhydb::query_engine::exec_node
