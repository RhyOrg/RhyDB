#include "rhydb/query_engine/scalar_expressions/float_between.h"

#include <cmath>
#include <memory>
#include <utility>

#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

using rhydb::storage::column::FloatColumn;

namespace rhydb::query_engine::scalar_expressions {

// NOLINTBEGIN(bugprone-easily-swappable-parameters,readability-identifier-length)
FloatBetween::FloatBetween(
   schema::ColumnIdentifier column,
   std::optional<double> from,
   std::optional<double> to
)
    // NOLINTEND(bugprone-easily-swappable-parameters,readability-identifier-length)
    : column(std::move(column)),
      from(from),
      to(to) {}

std::string FloatBetween::toString() const {
   const auto from_string = from.has_value() ? std::to_string(from.value()) : "unbounded";
   const auto to_string = to.has_value() ? std::to_string(to.value()) : "unbounded";

   return "[FloatBetween " + from_string + " - " + to_string + "]";
}

std::vector<schema::ColumnIdentifier> FloatBetween::freeIUs() const {
   return {column};
}

std::unique_ptr<ScalarExpression> FloatBetween::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<FloatBetween>(column, from, to);
}

std::unique_ptr<filter::operators::Operator> FloatBetween::compile(
   const storage::Table& table
) const {
   CHECK_RHYDB_QUERY(
      table.hasColumn<FloatColumn>(column.name),
      "The database does not contain the float column '{}'",
      column.name
   );
   const auto& float_column = table.getColumn<FloatColumn>(column.name);

   filter::operators::PredicateVector predicates;
   if (from.has_value()) {
      predicates.emplace_back(
         std::make_unique<filter::operators::CompareToValueSelection<FloatColumn>>(
            float_column, filter::operators::Comparator::HIGHER_OR_EQUALS, from.value()
         )
      );
   }

   if (to.has_value()) {
      predicates.emplace_back(
         std::make_unique<filter::operators::CompareToValueSelection<FloatColumn>>(
            float_column, filter::operators::Comparator::LESS, to.value()
         )
      );
   }

   if (predicates.empty()) {
      return std::make_unique<filter::operators::Complement>(
         std::make_unique<filter::operators::IndexScan>(
            Bitmap{&float_column.null_bitmap}, table.row_layout
         ),
         table.row_layout
      );
   }

   return std::make_unique<filter::operators::Selection>(std::move(predicates), table.row_layout);
}

}  // namespace rhydb::query_engine::scalar_expressions
