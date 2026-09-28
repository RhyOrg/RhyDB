#include "rhydb/query_engine/scalar_expressions/date_between.h"

#include <cstdint>
#include <limits>
#include <map>
#include <utility>

#include <fmt/format.h>

#include "rhydb/common/date32.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/range_selection.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/date32_column.h"

using rhydb::common::Date32;
using rhydb::common::date32ToString;
using rhydb::storage::column::Date32Column;

namespace rhydb::query_engine::scalar_expressions {

DateBetween::DateBetween(
   schema::ColumnIdentifier column,
   // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
   std::optional<Date32> date_from,
   std::optional<Date32> date_to
)
    : column(std::move(column)),
      date_from(date_from),
      date_to(date_to) {}

std::string DateBetween::toString() const {
   std::string res = "[Date-between ";
   res += (date_from.has_value() ? date32ToString(date_from.value()) : "unbounded");
   res += " and ";
   res += (date_to.has_value() ? date32ToString(date_to.value()) : "unbounded");
   res += "]";
   return res;
}

std::vector<schema::ColumnIdentifier> DateBetween::freeIUs() const {
   return {column};
}

std::unique_ptr<ScalarExpression> DateBetween::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<DateBetween>(column, date_from, date_to);
}

using filter::operators::Comparator;
using filter::operators::CompareToValueSelection;
using filter::operators::Operator;
using filter::operators::PredicateVector;
using filter::operators::RangeSelection;
using filter::operators::Selection;

std::unique_ptr<Operator> DateBetween::compile(const storage::Table& table) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.columns.date32_columns.contains(column.name),
      "The column '{}' is not of type date",
      column.name
   );

   const auto& date_column = table.columns.date32_columns.at(column.name);

   if (date_column.isSorted()) {
      return std::make_unique<RangeSelection>(
         computeRangesOfSortedColumn(date_column), table.row_layout
      );
   }
   PredicateVector predicates;
   predicates.emplace_back(std::make_unique<CompareToValueSelection<Date32Column>>(
      date_column,
      Comparator::HIGHER_OR_EQUALS,
      date_from.value_or(std::numeric_limits<Date32>::min())
   ));
   predicates.emplace_back(std::make_unique<CompareToValueSelection<Date32Column>>(
      date_column, Comparator::LESS_OR_EQUALS, date_to.value_or(std::numeric_limits<Date32>::max())
   ));
   return std::make_unique<Selection>(std::move(predicates), table.row_layout);
}

using storage::column::RowId;

std::vector<RangeSelection::Range> DateBetween::computeRangesOfSortedColumn(
   const Date32Column& date_column
) const {
   std::vector<RangeSelection::Range> ranges;

   const auto from = date_from.value_or(std::numeric_limits<Date32>::min());

   // The column is sorted globally, so each chunk is itself sorted and the chunks are in order. We
   // binary search within every chunk's value buffer and emit one range per chunk, shifted by the
   // chunk's global row offset.
   const auto& value_buffer = date_column.getValueBuffer();
   RHYDB_ASSERT(value_buffer.numChunks() <= UINT16_MAX);
   for (size_t chunk_idx = 0; chunk_idx < value_buffer.numChunks(); ++chunk_idx) {
      const auto& chunk = value_buffer.chunk(chunk_idx);
      const auto* begin = chunk.data();
      const auto* end = begin + chunk.size();
      const auto* lower = std::lower_bound(begin, end, from);
      const auto* upper = date_to.has_value() ? std::upper_bound(begin, end, date_to.value()) : end;

      const auto lower_index = lower - begin;
      const auto upper_index = upper - begin;

      const auto chunk_size = value_buffer.chunkSize(static_cast<uint16_t>(chunk_idx));
      const RowId start_row =
         (lower_index == chunk_size)
            ? RowId{.chunk_id = static_cast<uint16_t>(chunk_idx + 1), .row_in_chunk = 0}
            : RowId{
                 .chunk_id = static_cast<uint16_t>(chunk_idx),
                 .row_in_chunk = static_cast<uint16_t>(lower_index),
              };
      const RowId end_row =
         (upper_index == chunk_size)
            ? RowId{.chunk_id = static_cast<uint16_t>(chunk_idx + 1), .row_in_chunk = 0}
            : RowId{
                 .chunk_id = static_cast<uint16_t>(chunk_idx),
                 .row_in_chunk = static_cast<uint16_t>(upper_index),
              };
      ranges.emplace_back(start_row, end_row);
   }
   return ranges;
}

}  // namespace rhydb::query_engine::scalar_expressions
