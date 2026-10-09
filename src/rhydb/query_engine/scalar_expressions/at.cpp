#include "rhydb/query_engine/scalar_expressions/at.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::scalar_expressions {

At::At(std::unique_ptr<ScalarExpression> input, uint32_t position)
    : input(std::move(input)),
      position(position) {
   RHYDB_ASSERT(this->input != nullptr);
}

std::string At::toString() const {
   return fmt::format("{}.at({})", input->toString(), position);
}

std::vector<schema::ColumnIdentifier> At::freeIUs() const {
   return input->freeIUs();
}

arrow::Result<arrow::compute::Expression> At::toArrowExpression() const {
   // `at` is 1-indexed; utf8_slice_codeunits takes a 0-indexed, half-open
   // [start, stop) range of code units, so extract the single character at
   // position-1.
   const int64_t start = static_cast<int64_t>(position) - 1;
   const auto stop = static_cast<int64_t>(position);
   ARROW_ASSIGN_OR_RAISE(auto input_expression, input->toArrowExpression());
   return arrow::compute::call(
      "utf8_slice_codeunits", {input_expression}, arrow::compute::SliceOptions(start, stop)
   );
}

std::unique_ptr<ScalarExpression> At::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<At>(input->rewrite(table, mode), position);
}

std::unique_ptr<filter::operators::Operator> At::compile(const storage::Table& /*table*/) const {
   // `at` yields a string scalar value, not a filter predicate.
   RHYDB_UNIMPLEMENTED();
}

}  // namespace rhydb::query_engine::scalar_expressions
