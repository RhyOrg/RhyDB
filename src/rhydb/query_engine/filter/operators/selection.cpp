#include "rhydb/query_engine/filter/operators/selection.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <iterator>
#include <ranges>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <boost/algorithm/string/join.hpp>
#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/german_string.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::query_engine::filter::operators {

using storage::column::StringColumn;

Selection::Selection(
   std::optional<std::unique_ptr<Operator>> child_operator,
   std::vector<std::unique_ptr<Predicate>>&& predicates,
   storage::column::RowLayout row_layout
)
    : child_operator(std::move(child_operator)),
      predicates(std::move(predicates)),
      row_layout(std::move(row_layout)) {
   const auto row_count = this->row_layout.numRows();
   std::ranges::sort(this->predicates, [row_count](const auto& left, const auto& right) {
      return left->estimateSelectivity(row_count) < right->estimateSelectivity(row_count);
   });
}

Selection::Selection(
   std::unique_ptr<Operator>&& child_operator,
   std::vector<std::unique_ptr<Predicate>>&& predicates,
   storage::column::RowLayout row_layout
)
    : Selection(
         std::optional<std::unique_ptr<Operator>>{std::move(child_operator)},
         std::move(predicates),
         std::move(row_layout)
      ) {}

Selection::Selection(
   std::unique_ptr<Operator>&& child_operator,
   std::unique_ptr<Predicate> predicate,
   storage::column::RowLayout row_layout
)
    : child_operator(std::move(child_operator)),
      row_layout(std::move(row_layout)) {
   predicates.emplace_back(std::move(predicate));
}

Selection::Selection(
   std::vector<std::unique_ptr<Predicate>>&& predicates,
   storage::column::RowLayout row_layout
)
    : Selection(std::nullopt, std::move(predicates), std::move(row_layout)) {}

Selection::Selection(std::unique_ptr<Predicate> predicate, storage::column::RowLayout row_layout)
    : row_layout(std::move(row_layout)) {
   predicates.emplace_back(std::move(predicate));
}

Selection::~Selection() noexcept = default;

std::string Selection::toString() const {
   std::vector<std::string> predicate_strings;
   std::ranges::transform(
      predicates,
      std::back_inserter(predicate_strings),
      [](const auto& predicate) { return predicate->toString(); }
   );
   std::string child_operator_string;
   if (child_operator.has_value()) {
      child_operator_string = child_operator.value()->toString();
   }
   return "Select[" + boost::algorithm::join(predicate_strings, ",") + "](" +
          child_operator_string + ")";
}

Type Selection::type() const {
   return SELECTION;
}

Bitmap Selection::evaluate() const {
   EVOBENCH_SCOPE("Selection", "evaluate");
   RHYDB_ASSERT(!predicates.empty());

   // Build the candidate rows that already satisfy the most selective predicate. Predicates are
   // sorted most-selective-first at construction
   Bitmap candidates;
   if (child_operator.has_value()) {
      Bitmap child_bitmap = (*child_operator)->evaluate();
      // For a small child, matching each of its rows against every predicate is cheaper than
      // materializing the first predicate over the whole partition.
      if (child_bitmap.cardinality() <= row_layout.numRows() / 10) {
         roaring::Roaring result;
         for (const auto& [chunk_id, container_view] : child_bitmap) {
            for (const uint16_t row_in_chunk : container_view) {
               const storage::column::RowId row_id{
                  .chunk_id = chunk_id, .row_in_chunk = row_in_chunk,
               };
               if (matchesPredicates(predicates, row_id)) {
                  result.add(row_id.toGlobal());
               }
            }
         }
         return Bitmap{std::move(result)};
      }
      candidates = std::move(child_bitmap);
      candidates &= Bitmap{predicates.front()->makeBitmap(row_layout)};
   } else {
      candidates = Bitmap{predicates.front()->makeBitmap(row_layout)};
   }

   // `candidates` already satisfies predicates.front(); apply the remaining predicates row by row.
   if (predicates.size() == 1) {
      return Bitmap{std::move(candidates)};
   }
   const auto remaining_predicates =
      std::ranges::subrange(predicates.begin() + 1, predicates.end());
   roaring::Roaring result;
   for (const auto& [chunk_id, container_view] : candidates) {
      for (const uint16_t row_in_chunk : container_view) {
         const storage::column::RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
         if (matchesPredicates(remaining_predicates, row_id)) {
            result.add(row_id.toGlobal());
         }
      }
   }
   return Bitmap{std::move(result)};
}

std::unique_ptr<Operator> Selection::negate(std::unique_ptr<Selection>&& selection) {
   auto row_layout = selection->row_layout;
   if (selection->child_operator == std::nullopt && selection->predicates.size() == 1) {
      return std::make_unique<Selection>(
         selection->predicates.at(0)->negate(), std::move(row_layout)
      );
   }
   return std::make_unique<Complement>(std::move(selection), std::move(row_layout));
}

namespace {

bool strongOrderingMatchesComparator(std::strong_ordering strong_ordering, Comparator comparator) {
   if (strong_ordering == std::strong_ordering::equal) {
      return comparator == Comparator::HIGHER_OR_EQUALS ||
             comparator == Comparator::LESS_OR_EQUALS || comparator == Comparator::EQUALS;
   }
   if (strong_ordering == std::strong_ordering::less) {
      return comparator == Comparator::LESS || comparator == Comparator::LESS_OR_EQUALS ||
             comparator == Comparator::NOT_EQUALS;
   }
   if (strong_ordering == std::strong_ordering::greater) {
      return comparator == Comparator::HIGHER || comparator == Comparator::HIGHER_OR_EQUALS ||
             comparator == Comparator::NOT_EQUALS;
   }
   RHYDB_UNREACHABLE();
}

}  // namespace

template <>
bool CompareToValueSelection<StringColumn>::match(storage::column::RowId row_id) const {
   if (column.isNull(row_id)) {
      return with_nulls;
   }

   const RhyDBString row_value = column.getValue(row_id);

   auto fast_compare = row_value.fastCompare(value);
   if (fast_compare.has_value()) {
      return strongOrderingMatchesComparator(fast_compare.value(), comparator);
   }

   auto row_value_string = column.getValueString(row_id);

   // Slower fall-back if the prefix could not decide the match. Uses unsigned byte
   // ordering so it agrees with the short-string and dictionary comparison paths.
   const std::strong_ordering strong_ordering = compareBytesUnsigned(row_value_string, value);
   return strongOrderingMatchesComparator(strong_ordering, comparator);
}

}  // namespace rhydb::query_engine::filter::operators
