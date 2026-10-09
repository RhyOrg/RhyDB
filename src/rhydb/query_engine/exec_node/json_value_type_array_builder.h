#pragma once

#include <memory>
#include <optional>
#include <variant>

#include <arrow/builder.h>

namespace rhydb::query_engine::exec_node {

class JsonValueTypeArrayBuilder {
   std::variant<
      arrow::Int32Builder,
      arrow::DoubleBuilder,
      arrow::StringBuilder,
      arrow::BooleanBuilder>
      builder;

  public:
   explicit JsonValueTypeArrayBuilder(const std::shared_ptr<arrow::DataType>& type);

   arrow::Status insert(
      const std::optional<std::variant<std::string, bool, int32_t, double>>& value
   );

   arrow::Result<arrow::Datum> toDatum();
};

}  // namespace rhydb::query_engine::exec_node
