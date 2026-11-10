#include "rhydb/query_engine/exec_node/cast_expression.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/exec.h>
#include <arrow/compute/kernel.h>
#include <arrow/compute/registry.h>
#include <arrow/datum.h>
#include <arrow/util/checked_cast.h>
#include <fmt/format.h>

#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/query_execution_error.h"

namespace rhydb::query_engine::exec_node {

namespace {

/// Carries the requested target type from the call's `CastOptions` to the output-type resolver and
/// the kernel, so a single registered function can cast to any target instead of one function per
/// type.
struct TargetTypeState : arrow::compute::KernelState {
   explicit TargetTypeState(std::shared_ptr<arrow::DataType> type)
       : type(std::move(type)) {}
   std::shared_ptr<arrow::DataType> type;
};

arrow::Result<std::unique_ptr<arrow::compute::KernelState>> initTargetType(
   arrow::compute::KernelContext* /*context*/,
   const arrow::compute::KernelInitArgs& args
) {
   if (args.options == nullptr) {
      return arrow::Status::Invalid("rhydb_cast requires CastOptions carrying the target type");
   }
   const auto& options =
      arrow::internal::checked_cast<const arrow::compute::CastOptions&>(*args.options);
   return std::make_unique<TargetTypeState>(options.to_type.GetSharedPtr());
}

arrow::Result<arrow::TypeHolder> resolveTargetType(
   arrow::compute::KernelContext* context,
   const std::vector<arrow::TypeHolder>& /*inputs*/
) {
   return arrow::TypeHolder{
      arrow::internal::checked_cast<const TargetTypeState*>(context->state())->type
   };
}

/// Delegates to Arrow's `cast` and, on failure, tags the status so a bad column value is reported
/// as a user/query error rather than an internal RhyDB bug. The target type comes from the kernel's
/// resolved output type (see `initTargetType`/`resolveTargetType`).
arrow::Status taggedCastKernel(
   arrow::compute::KernelContext* context,
   const arrow::compute::ExecSpan& input,
   arrow::compute::ExecResult* out
) {
   ARROW_ASSIGN_OR_RAISE(
      auto input_array, execValueAsArray(input[0], input.length, context->memory_pool())
   );
   arrow::compute::ExecContext exec_context{context->memory_pool()};
   auto result = arrow::compute::Cast(
      arrow::Datum{input_array},
      arrow::compute::CastOptions::Safe(out->type()->GetSharedPtr()),
      &exec_context
   );
   if (!result.ok()) {
      return markAsQueryExecutionError(result.status());
   }
   *out = arrow::compute::ExecResult{.value = result->array()};
   return arrow::Status::OK();
}

/// Registers the single tagging cast function once and returns its name. The registry is shared and
/// append-only, so registration happens exactly once.
std::string registerCastFunction() {
   const std::string function_name = "rhydb_cast";
   const std::string summary = "Casts to a target type, tagging failures as query errors";
   auto function = std::make_shared<arrow::compute::ScalarFunction>(
      function_name,
      arrow::compute::Arity::Unary(),
      arrow::compute::FunctionDoc(summary, summary, {"input"})
   );

   arrow::compute::ScalarKernel kernel;
   kernel.init = initTargetType;
   kernel.exec = taggedCastKernel;
   kernel.signature = arrow::compute::KernelSignature::Make(
      {arrow::compute::InputType::Any()}, arrow::compute::OutputType{resolveTargetType}
   );
   kernel.null_handling = arrow::compute::NullHandling::COMPUTED_NO_PREALLOCATE;
   kernel.mem_allocation = arrow::compute::MemAllocation::NO_PREALLOCATE;

   auto status = function->AddKernel(std::move(kernel));
   if (status.ok()) {
      status = arrow::compute::GetFunctionRegistry()->AddFunction(std::move(function));
   }
   if (!status.ok()) {
      throw std::runtime_error(fmt::format("Err: {}", status.ToString()));
   }
   return function_name;
}

}  // namespace

arrow::compute::Expression CastExpression::make(
   arrow::compute::Expression input_expression,
   std::shared_ptr<arrow::DataType> target_type
) {
   static const std::string function_name = registerCastFunction();
   return arrow::compute::call(
      function_name,
      {std::move(input_expression)},
      arrow::compute::CastOptions::Safe(std::move(target_type))
   );
}

}  // namespace rhydb::query_engine::exec_node
