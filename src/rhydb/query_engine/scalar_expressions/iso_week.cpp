#include "rhydb/query_engine/scalar_expressions/iso_week.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::scalar_expressions {

IsoWeek::IsoWeek(std::unique_ptr<ScalarExpression> input)
    : input(std::move(input)) {
   RHYDB_ASSERT(this->input != nullptr);
}

std::string IsoWeek::toString() const {
   return fmt::format("{}.isoWeek()", input->toString());
}

std::vector<schema::ColumnIdentifier> IsoWeek::freeIUs() const {
   return input->freeIUs();
}

arrow::Result<arrow::compute::Expression> IsoWeek::toArrowExpression() const {
   ARROW_ASSIGN_OR_RAISE(auto input_expression, input->toArrowExpression());
   // Render the ISO 8601 week date `<ISO-year>-W<ISO-week>`, e.g. `2026-W12`
   const auto year_string = arrow::compute::call(
      "cast",
      {arrow::compute::call("iso_year", {input_expression})},
      arrow::compute::CastOptions::Safe(arrow::utf8())
   );
   const auto week_string = arrow::compute::call(
      "cast",
      {arrow::compute::call("iso_week", {input_expression})},
      arrow::compute::CastOptions::Safe(arrow::utf8())
   );
   // Zero-pad the week to two digits so `2021-W02` sorts before `2021-W10`.
   const auto week_padded =
      arrow::compute::call("utf8_lpad", {week_string}, arrow::compute::PadOptions(2, "0"));
   // Join `<year> + "-W" + <week>` -- third argument is separator
   return arrow::compute::call(
      "binary_join_element_wise", {year_string, week_padded, arrow::compute::literal("-W")}
   );
}

std::unique_ptr<ScalarExpression> IsoWeek::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<IsoWeek>(input->rewrite(table, mode));
}

std::unique_ptr<filter::operators::Operator> IsoWeek::
   compile(const storage::Table& /*table*/) const {
   RHYDB_UNIMPLEMENTED();
}

}  // namespace rhydb::query_engine::scalar_expressions
