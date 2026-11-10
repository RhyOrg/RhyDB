#include "rhydb/query_engine/exec_node/tagged_cast_expression.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <arrow/compute/exec.h>
#include <arrow/compute/kernel.h>
#include <arrow/compute/registry.h>
#include <arrow/datum.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/query_execution_error.h"

namespace rhydb::query_engine::exec_node {

namespace {

/// Delegates to Arrow's `cast` to `TargetType` and, on failure, tags the status so that a bad
/// column value is reported as a user/query error rather than an internal RhyDB bug. `TargetType`
/// is fixed per registered function so the stateless Arrow kernel (a plain function pointer) knows
/// what to cast to.
template <typename TargetType>
arrow::Status taggedCastKernel(
   arrow::compute::KernelContext* context,
   const arrow::compute::ExecSpan& input,
   arrow::compute::ExecResult* out
) {
   if (input.num_values() != 1) {
      return arrow::Status::Invalid("Expected 1 input, got ", input.num_values());
   }
   ARROW_ASSIGN_OR_RAISE(
      auto input_array, execValueAsArray(input[0], input.length, context->memory_pool())
   );
   arrow::compute::ExecContext exec_context{context->memory_pool()};
   const std::shared_ptr<arrow::DataType> target_type = std::make_shared<TargetType>();
   auto result = arrow::compute::Cast(
      arrow::Datum{input_array}, arrow::compute::CastOptions::Safe(target_type), &exec_context
   );
   if (!result.ok()) {
      return markAsQueryExecutionError(result.status());
   }
   *out = arrow::compute::ExecResult{.value = result->array()};
   return arrow::Status::OK();
}

template <typename TargetType>
void registerTaggedCastFunction(
   const std::string& function_name,
   const std::shared_ptr<arrow::DataType>& output_type
) {
   const std::string summary = "Casts to " + output_type->ToString() + ", tagging failures";
   auto function = std::make_shared<arrow::compute::ScalarFunction>(
      function_name,
      arrow::compute::Arity::Unary(),
      arrow::compute::FunctionDoc(summary, summary, {"input"})
   );

   arrow::compute::ScalarKernel kernel;
   kernel.exec = taggedCastKernel<TargetType>;
   kernel.signature =
      arrow::compute::KernelSignature::Make({arrow::compute::InputType::Any()}, output_type);
   kernel.null_handling = arrow::compute::NullHandling::COMPUTED_NO_PREALLOCATE;
   kernel.mem_allocation = arrow::compute::MemAllocation::NO_PREALLOCATE;

   auto status = function->AddKernel(std::move(kernel));
   if (status.ok()) {
      status = arrow::compute::GetFunctionRegistry()->AddFunction(std::move(function));
   }
   if (!status.ok()) {
      throw std::runtime_error(fmt::format("Err: {}", status.ToString()));
   }
}

/// Registers one tagged-cast function per target type RhyDB casts to and returns the function name
/// for `target_type`. The registry is shared and append-only, so registration happens exactly once.
std::string registerAndResolve(const std::shared_ptr<arrow::DataType>& target_type) {
   [[maybe_unused]] static const bool registered = [] {
      registerTaggedCastFunction<arrow::Int32Type>("rhydb_cast_to_int32", arrow::int32());
      registerTaggedCastFunction<arrow::Int64Type>("rhydb_cast_to_int64", arrow::int64());
      registerTaggedCastFunction<arrow::DoubleType>("rhydb_cast_to_double", arrow::float64());
      registerTaggedCastFunction<arrow::Date32Type>("rhydb_cast_to_date32", arrow::date32());
      registerTaggedCastFunction<arrow::LargeStringType>(
         "rhydb_cast_to_large_utf8", arrow::large_utf8()
      );
      return true;
   }();

   switch (target_type->id()) {
      case arrow::Type::INT32:
         return "rhydb_cast_to_int32";
      case arrow::Type::INT64:
         return "rhydb_cast_to_int64";
      case arrow::Type::DOUBLE:
         return "rhydb_cast_to_double";
      case arrow::Type::DATE32:
         return "rhydb_cast_to_date32";
      case arrow::Type::LARGE_STRING:
         return "rhydb_cast_to_large_utf8";
      default:
         RHYDB_PANIC("No tagged cast registered for target type {}", target_type->ToString());
   }
}

}  // namespace

arrow::compute::Expression TaggedCastExpression::make(
   arrow::compute::Expression input_expression,
   const std::shared_ptr<arrow::DataType>& target_type
) {
   RHYDB_ASSERT(target_type != nullptr);
   const std::string function_name = registerAndResolve(target_type);
   return arrow::compute::call(function_name, {std::move(input_expression)});
}

}  // namespace rhydb::query_engine::exec_node
