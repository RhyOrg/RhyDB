#include "rhydb/query_engine/arrow_compute.h"

#include <arrow/compute/initialize.h>
#include <arrow/status.h>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine {

void initializeArrowCompute() {
   static const arrow::Status status = arrow::compute::Initialize();
   if (!status.ok()) {
      RHYDB_PANIC("Failed to register Arrow's compute functions: {}", status.ToString());
   }
}

}  // namespace rhydb::query_engine
