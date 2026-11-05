#include "rhydb/query_engine/scalar_expressions/cast.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/array/util.h>
#include <arrow/compute/api.h>
#include <arrow/scalar.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/string_to_bool_expression.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"

namespace rhydb::query_engine::scalar_expressions {

using exec_node::columnTypeToArrowType;
using exec_node::StringToBoolExpression;
using filter::operators::Operator;
using schema::ColumnIdentifier;
using schema::DataType;
using storage::Table;

namespace {

bool isNumeric(const DataType& type) {
   return type == DataType::INT32 || type == DataType::INT64 || type == DataType::FLOAT;
}

/// The value of the single-element `scalar` as a literal of `type`.
arrow::Result<std::unique_ptr<ScalarExpression>> scalarToLiteral(
   const arrow::Scalar& scalar,
   const DataType& type
) {
   if (!scalar.is_valid) {
      return std::make_unique<NullLiteral>(type);
   }
   if (type == DataType::STRING) {
      return std::make_unique<StringLiteral>(
         std::string{static_cast<const arrow::LargeStringScalar&>(scalar).view()}
      );
   }
   if (type == DataType::DATE32) {
      return std::make_unique<DateLiteral>(static_cast<const arrow::Date32Scalar&>(scalar).value);
   }
   if (type == DataType::BOOL) {
      return std::make_unique<BoolLiteral>(static_cast<const arrow::BooleanScalar&>(scalar).value);
   }
   if (type == DataType::INT32) {
      return std::make_unique<Int32Literal>(static_cast<const arrow::Int32Scalar&>(scalar).value);
   }
   if (type == DataType::INT64) {
      return std::make_unique<Int64Literal>(static_cast<const arrow::Int64Scalar&>(scalar).value);
   }
   if (type == DataType::FLOAT) {
      return std::make_unique<FloatLiteral>(static_cast<const arrow::DoubleScalar&>(scalar).value);
   }
   return arrow::Status::NotImplemented(
      "Cannot represent a value of type ", schema::dataTypeToString(type), " as a literal"
   );
}

}  // namespace

bool isCastSupported(const DataType& from, const DataType& to) {
   if (from == to) {
      return true;
   }
   if (isNumeric(from) && isNumeric(to)) {
      return true;
   }
   if (from == DataType::STRING) {
      return isNumeric(to) || to == DataType::DATE32 || to == DataType::BOOL;
   }
   return to == DataType::STRING &&
          (isNumeric(from) || from == DataType::DATE32 || from == DataType::BOOL);
}

Cast::Cast(std::unique_ptr<ScalarExpression> input, DataType target_type)
    : input(std::move(input)),
      target_type(std::move(target_type)) {
   RHYDB_ASSERT(this->input != nullptr);
}

std::string Cast::toString() const {
   return fmt::format("{}::{}", input->toString(), schema::toSchemaTypeName(target_type));
}

std::vector<ColumnIdentifier> Cast::freeIUs() const {
   return input->freeIUs();
}

arrow::Result<arrow::compute::Expression> Cast::toArrowExpression() const {
   const DataType source_type = input->type().getPlainType();
   ARROW_ASSIGN_OR_RAISE(auto input_expression, input->toArrowExpression());
   if (source_type == target_type) {
      return input_expression;
   }
   if (source_type == DataType::STRING && target_type == DataType::BOOL) {
      // Arrow's own cast also accepts e.g. '1' and 'TRUE'
      return StringToBoolExpression::make(std::move(input_expression));
   }
   return arrow::compute::call(
      "cast",
      {std::move(input_expression)},
      arrow::compute::CastOptions::Safe(columnTypeToArrowType(target_type))
   );
}

std::unique_ptr<ScalarExpression> Cast::rewrite(
   const Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<Cast>(input->rewrite(table, mode), target_type);
}

std::unique_ptr<Operator> Cast::compile(const Table& /*table*/) const {
   throw IllegalQueryException(
      "A type cast cannot be evaluated in a filter on a table scan. Compute the value with map() "
      "first, e.g. data.map({x := a::float}).filter(x < 3.5)"
   );
}

arrow::Result<std::unique_ptr<ScalarExpression>> castConstant(
   const ScalarExpression& input,
   const DataType& target_type
) {
   const Cast cast{input.clone(), target_type};
   ARROW_ASSIGN_OR_RAISE(auto expression, cast.toArrowExpression());
   ARROW_ASSIGN_OR_RAISE(expression, expression.Bind(arrow::Schema{{}}));
   ARROW_ASSIGN_OR_RAISE(
      auto value,
      arrow::compute::ExecuteScalarExpression(expression, arrow::compute::ExecBatch{{}, 1})
   );
   if (value.is_scalar()) {
      return scalarToLiteral(*value.scalar(), target_type);
   }
   ARROW_ASSIGN_OR_RAISE(auto scalar, value.make_array()->GetScalar(0));
   return scalarToLiteral(*scalar, target_type);
}

}  // namespace rhydb::query_engine::scalar_expressions
