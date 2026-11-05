#include "rhydb/query_engine/scalar_expressions/literal.h"

#include <memory>
#include <string>
#include <utility>

#include <arrow/compute/api.h>
#include <arrow/datum.h>
#include <arrow/scalar.h>
#include <arrow/type.h>
#include <fmt/format.h>

#include "rhydb/common/date32.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::scalar_expressions {

using filter::operators::Empty;
using storage::Table;

namespace {
/// Literals are scalar values, not filter predicates. Only the boolean literal
/// has a meaningful compilation into a filter operator.
[[noreturn]] std::unique_ptr<filter::operators::Operator> compileNonBooleanLiteral() {
   RHYDB_UNIMPLEMENTED();
}
}  // namespace

Int32Literal::Int32Literal(int32_t value)
    : value(value) {}

std::string Int32Literal::toString() const {
   return fmt::format("{}", value);
}

arrow::Result<arrow::compute::Expression> Int32Literal::toArrowExpression() const {
   return arrow::compute::literal(arrow::Datum(value));
}

std::unique_ptr<ScalarExpression> Int32Literal::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<Int32Literal>(value);
}

std::unique_ptr<filter::operators::Operator> Int32Literal::compile(const Table& /*table*/) const {
   compileNonBooleanLiteral();
}

Int64Literal::Int64Literal(int64_t value)
    : value(value) {}

std::string Int64Literal::toString() const {
   return fmt::format("{}", value);
}

arrow::Result<arrow::compute::Expression> Int64Literal::toArrowExpression() const {
   return arrow::compute::literal(arrow::Datum(value));
}

std::unique_ptr<ScalarExpression> Int64Literal::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<Int64Literal>(value);
}

std::unique_ptr<filter::operators::Operator> Int64Literal::compile(const Table& /*table*/) const {
   compileNonBooleanLiteral();
}

FloatLiteral::FloatLiteral(double value)
    : value(value) {}

std::string FloatLiteral::toString() const {
   return fmt::format("{}", value);
}

arrow::Result<arrow::compute::Expression> FloatLiteral::toArrowExpression() const {
   return arrow::compute::literal(arrow::Datum(value));
}

std::unique_ptr<ScalarExpression> FloatLiteral::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<FloatLiteral>(value);
}

std::unique_ptr<filter::operators::Operator> FloatLiteral::compile(const Table& /*table*/) const {
   compileNonBooleanLiteral();
}

StringLiteral::StringLiteral(std::string value)
    : value(std::move(value)) {}

std::string StringLiteral::toString() const {
   return fmt::format("'{}'", value);
}

arrow::Result<arrow::compute::Expression> StringLiteral::toArrowExpression() const {
   return arrow::compute::literal(arrow::Datum(std::make_shared<arrow::LargeStringScalar>(value)));
}

std::unique_ptr<ScalarExpression> StringLiteral::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<StringLiteral>(value);
}

std::unique_ptr<filter::operators::Operator> StringLiteral::compile(const Table& /*table*/) const {
   compileNonBooleanLiteral();
}

BoolLiteral::BoolLiteral(bool value)
    : value(value) {}

std::string BoolLiteral::toString() const {
   return value ? "true" : "false";
}

arrow::Result<arrow::compute::Expression> BoolLiteral::toArrowExpression() const {
   return arrow::compute::literal(arrow::Datum(value));
}

std::unique_ptr<ScalarExpression> BoolLiteral::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<BoolLiteral>(value);
}

std::unique_ptr<filter::operators::Operator> BoolLiteral::compile(const Table& table) const {
   if (value) {
      return std::make_unique<filter::operators::Full>(table.row_layout);
   }
   return std::make_unique<Empty>(table.row_layout);
}

DateLiteral::DateLiteral(common::Date32 value)
    : value(value) {}

std::string DateLiteral::toString() const {
   return fmt::format("'{}'", common::date32ToString(value));
}

arrow::Result<arrow::compute::Expression> DateLiteral::toArrowExpression() const {
   return arrow::compute::literal(arrow::Datum(std::make_shared<arrow::Date32Scalar>(value)));
}

std::unique_ptr<ScalarExpression> DateLiteral::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<DateLiteral>(value);
}

std::unique_ptr<filter::operators::Operator> DateLiteral::compile(const Table& /*table*/) const {
   compileNonBooleanLiteral();
}

NullLiteral::NullLiteral(schema::DataType value_type)
    : value_type(std::move(value_type)) {}

std::string NullLiteral::toString() const {
   return "null";
}

arrow::Result<arrow::compute::Expression> NullLiteral::toArrowExpression() const {
   return arrow::compute::literal(
      arrow::Datum(arrow::MakeNullScalar(exec_node::columnTypeToArrowType(value_type)))
   );
}

std::unique_ptr<ScalarExpression> NullLiteral::rewrite(
   const Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<NullLiteral>(value_type);
}

std::unique_ptr<filter::operators::Operator> NullLiteral::compile(const Table& table) const {
   if (value_type == schema::DataType::BOOL) {
      // A null predicate matches no row, like a comparison with a null cell.
      return std::make_unique<Empty>(table.row_layout);
   }
   compileNonBooleanLiteral();
}

}  // namespace rhydb::query_engine::scalar_expressions
