#include "rhydb/query_engine/exec_node/string_to_bool_expression.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <arrow/api.h>
#include <arrow/compute/api.h>
#include <arrow/compute/expression.h>
#include <arrow/compute/kernel.h>
#include <arrow/compute/registry.h>
#include <fmt/format.h>

#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/query_execution_error.h"

namespace rhydb::query_engine::exec_node {

namespace {

arrow::Status appendBoolean(arrow::BooleanBuilder& builder, std::string_view text) {
   if (text == "true") {
      return builder.Append(true);
   }
   if (text == "false") {
      return builder.Append(false);
   }
   return markAsQueryExecutionError(
      arrow::Status::Invalid(
         fmt::format("Failed to parse '{}' as boolean: expected 'true' or 'false'", text)
      )
   );
}

arrow::Result<std::shared_ptr<arrow::Array>> parseBooleans(
   const arrow::LargeStringArray& strings,
   arrow::MemoryPool* pool
) {
   arrow::BooleanBuilder builder(pool);
   ARROW_RETURN_NOT_OK(builder.Reserve(strings.length()));
   for (int64_t index = 0; index < strings.length(); ++index) {
      ARROW_RETURN_NOT_OK(
         strings.IsNull(index) ? builder.AppendNull()
                               : appendBoolean(builder, strings.GetView(index))
      );
   }
   return builder.Finish();
}

arrow::Status stringToBoolKernel(
   arrow::compute::KernelContext* context,
   const arrow::compute::ExecSpan& input,
   arrow::compute::ExecResult* out
) {
   if (input.num_values() != 1) {
      return arrow::Status::Invalid("Expected 1 input, got ", input.num_values());
   }
   ARROW_ASSIGN_OR_RAISE(
      auto input_array, execValueAsArray(input.values[0], input.length, context->memory_pool())
   );
   ARROW_ASSIGN_OR_RAISE(
      auto output_array,
      parseBooleans(
         static_cast<const arrow::LargeStringArray&>(*input_array), context->memory_pool()
      )
   );
   *out = arrow::compute::ExecResult{.value = output_array->data()};
   return arrow::Status::OK();
}

std::string registerStringToBoolFunction() {
   const std::string function_name = "rhydb_cast_string_to_bool";
   const std::string summary = "Casts 'true'/'false' strings to boolean";
   auto function = std::make_shared<arrow::compute::ScalarFunction>(
      function_name,
      arrow::compute::Arity::Unary(),
      arrow::compute::FunctionDoc(summary, summary, {"string"})
   );

   arrow::compute::ScalarKernel kernel;
   kernel.exec = stringToBoolKernel;
   kernel.signature =
      arrow::compute::KernelSignature::Make({arrow::large_utf8()}, arrow::boolean());
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

arrow::compute::Expression StringToBoolExpression::make(
   arrow::compute::Expression input_expression
) {
   static const std::string function_name = registerStringToBoolFunction();
   return arrow::compute::call(function_name, {std::move(input_expression)});
}

}  // namespace rhydb::query_engine::exec_node
