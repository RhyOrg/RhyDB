#include "rhydb/query_engine/exec_node/json_value_type_array_builder.h"

#include <arrow/datum.h>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine::exec_node {

JsonValueTypeArrayBuilder::JsonValueTypeArrayBuilder(const std::shared_ptr<arrow::DataType>& type) {
   if (type == arrow::int32()) {
      builder = arrow::Int32Builder{};
   } else if (type == arrow::float64()) {
      builder = arrow::DoubleBuilder{};
   } else if (type == arrow::utf8()) {
      builder = arrow::StringBuilder{};
   } else if (type == arrow::boolean()) {
      builder = arrow::BooleanBuilder{};
   } else {
      RHYDB_PANIC("Invalid type found: ", type->ToString());
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Status JsonValueTypeArrayBuilder::insert(
   const std::optional<std::variant<std::string, bool, int32_t, double>>& value
) {
   if (!value.has_value()) {
      return std::visit(
         [](auto& builder) {
            ARROW_RETURN_NOT_OK(builder.AppendNull());
            return arrow::Status::OK();
         },
         builder
      );
   }

   // NOLINTBEGIN(readability-function-cognitive-complexity)
   return std::visit(
      [this](auto&& val) {
         using T = std::decay_t<decltype(val)>;
         return std::visit(
            [&val](auto& builder) {
               using B = std::decay_t<decltype(builder)>;
               if constexpr (std::is_same_v<T, int32_t> && std::is_same_v<B, arrow::Int32Builder>) {
                  ARROW_RETURN_NOT_OK(builder.Append(val));
               } else if constexpr (
                  std::is_same_v<T, double> && std::is_same_v<B, arrow::DoubleBuilder>
               ) {
                  ARROW_RETURN_NOT_OK(builder.Append(val));
               } else if constexpr (
                  std::is_same_v<T, std::string> && std::is_same_v<B, arrow::StringBuilder>
               ) {
                  ARROW_RETURN_NOT_OK(builder.Append(val));
               } else if constexpr (
                  std::is_same_v<T, bool> && std::is_same_v<B, arrow::BooleanBuilder>
               ) {
                  ARROW_RETURN_NOT_OK(builder.Append(val));
               } else {
                  RHYDB_PANIC("Type mismatch between value and builder");
               }
               return arrow::Status::OK();
            },
            builder
         );
      },
      value.value()
   );
   // NOLINTEND(readability-function-cognitive-complexity)
}

arrow::Result<arrow::Datum> JsonValueTypeArrayBuilder::toDatum() {
   return std::visit([](auto& builder) { return builder.Finish(); }, builder).Map([](auto&& array) {
      return arrow::Datum{array};
   });
}

}  // namespace rhydb::query_engine::exec_node
