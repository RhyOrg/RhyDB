#include "rhydb/query_engine/scalar_expressions/int_between.h"

#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/table.h"

using rhydb::storage::column::Int32Column;
using rhydb::storage::column::Int64Column;

namespace rhydb::query_engine::scalar_expressions {

namespace {
/// An integer bound is kept as a full-range int64; when the target column is int32 it must fit
/// int32 range. Raises the same out-of-range error the build step used to raise for an int32
/// literal.
void checkBoundFitsInt32(std::optional<int64_t> bound) {
   if (bound.has_value()) {
      CHECK_RHYDB_QUERY(
         bound.value() >= std::numeric_limits<int32_t>::min() &&
            bound.value() <= std::numeric_limits<int32_t>::max(),
         "Cannot cast {} to int32. Value out of range",
         bound.value()
      );
   }
}
}  // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters,readability-identifier-length)
IntBetween::IntBetween(
   schema::ColumnIdentifier column,
   std::optional<int64_t> from,
   std::optional<int64_t> to
)
    // NOLINTEND(bugprone-easily-swappable-parameters,readability-identifier-length)
    : column(std::move(column)),
      from(from),
      to(to) {}

std::string IntBetween::toString() const {
   const auto from_string = from.has_value() ? std::to_string(from.value()) : "unbounded";
   const auto to_string = to.has_value() ? std::to_string(to.value()) : "unbounded";

   return "[IntBetween " + from_string + " - " + to_string + "]";
}

std::vector<schema::ColumnIdentifier> IntBetween::freeIUs() const {
   return {column};
}

std::unique_ptr<ScalarExpression> IntBetween::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<IntBetween>(column, from, to);
}

template <typename ColumnT>
std::unique_ptr<filter::operators::Operator> IntBetween::compileFor(
   const ColumnT& column_ref,
   const storage::Table& table
) const {
   using value_type = ColumnT::value_type;
   filter::operators::PredicateVector predicates;
   if (from.has_value()) {
      predicates.emplace_back(
         std::make_unique<filter::operators::CompareToValueSelection<ColumnT>>(
            column_ref,
            filter::operators::Comparator::HIGHER_OR_EQUALS,
            static_cast<value_type>(from.value())
         )
      );
   }
   if (to.has_value()) {
      predicates.emplace_back(
         std::make_unique<filter::operators::CompareToValueSelection<ColumnT>>(
            column_ref,
            filter::operators::Comparator::LESS_OR_EQUALS,
            static_cast<value_type>(to.value())
         )
      );
   }

   if (predicates.empty()) {
      return std::make_unique<filter::operators::Complement>(
         std::make_unique<filter::operators::IndexScan>(
            Bitmap{&column_ref.null_bitmap}, table.row_layout
         ),
         table.row_layout
      );
   }

   auto result =
      std::make_unique<filter::operators::Selection>(std::move(predicates), table.row_layout);

   SPDLOG_TRACE("Compiled IntBetween filter expression to {}", result->toString());

   return std::move(result);
}

std::unique_ptr<filter::operators::Operator> IntBetween::compile(
   const storage::Table& table
) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.hasColumn<Int32Column>(column.name) || table.hasColumn<Int64Column>(column.name),
      "The column '{}' is not of type int32 or int64",
      column.name
   );

   if (table.hasColumn<Int64Column>(column.name)) {
      return compileFor<Int64Column>(table.getColumn<Int64Column>(column.name), table);
   }
   checkBoundFitsInt32(from);
   checkBoundFitsInt32(to);
   return compileFor<Int32Column>(table.getColumn<Int32Column>(column.name), table);
}

}  // namespace rhydb::query_engine::scalar_expressions
