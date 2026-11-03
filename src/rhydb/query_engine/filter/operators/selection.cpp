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
    : Operator(std::move(row_layout)),
      child_operator(std::move(child_operator)),
      predicates(std::move(predicates)) {
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
    : Operator(std::move(row_layout)),
      child_operator(std::move(child_operator)) {
   predicates.emplace_back(std::move(predicate));
}

Selection::Selection(
   std::vector<std::unique_ptr<Predicate>>&& predicates,
   storage::column::RowLayout row_layout
)
    : Selection(std::nullopt, std::move(predicates), std::move(row_layout)) {}

Selection::Selection(std::unique_ptr<Predicate> predicate, storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)) {
   predicates.emplace_back(std::move(predicate));
}

Selection::~Selection() noexcept = default;

std::string Selection::toString() const {
   std::vector<std::string> predicate_strings;
   std::ranges::transform(
      predicates, std::back_inserter(predicate_strings), [](const auto& predicate) {
         return predicate->toString();
      }
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

namespace {

/// The rows of `rows`, which belong to chunk `chunk_id`, that match all `predicates`
template <std::ranges::range PredicateRange>
roaring_util::RoaringContainer filterRows(
   roaring_util::RoaringContainerView rows,
   const PredicateRange& predicates,
   uint16_t chunk_id
) {
   auto result = roaring_util::RoaringContainer::withCapacity(
      static_cast<int32_t>(std::max(rows.getCardinality(), uint32_t{1}))
   );
   for (const uint16_t row_in_chunk : rows) {
      const storage::column::RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      if (std::ranges::all_of(predicates, [row_id](const auto& predicate) {
             return predicate->match(row_id);
          })) {
         result.add(row_in_chunk);
      }
   }
   return result;
}

}  // namespace

roaring_util::CopyOnWriteContainer Selection::evaluateChunk(uint16_t chunk_id) const {
   EVOBENCH_SCOPE("Selection", "evaluateChunk");
   RHYDB_ASSERT(!predicates.empty());
   const uint32_t chunk_size = row_layout.chunkSize(chunk_id);

   // Build the candidate rows that already satisfy the most selective predicate. Predicates are
   // sorted most-selective-first at construction
   roaring_util::CopyOnWriteContainer candidates;
   if (child_operator.has_value()) {
      auto child_rows = (*child_operator)->evaluateChunk(chunk_id);
      if (child_rows.empty()) {
         return child_rows;
      }
      // For a small child, matching each of its rows against every predicate is cheaper than
      // materializing the first predicate over the whole chunk.
      if (child_rows.getCardinality() <= chunk_size / 10) {
         return roaring_util::CopyOnWriteContainer{
            filterRows(child_rows.view(), predicates, chunk_id)
         };
      }
      candidates = std::move(child_rows);
      candidates &=
         roaring_util::RoaringContainerView{predicates.front()->makeChunk(chunk_id, chunk_size)};
   } else {
      candidates =
         roaring_util::CopyOnWriteContainer{predicates.front()->makeChunk(chunk_id, chunk_size)};
   }

   // `candidates` already satisfies predicates.front(); apply the remaining predicates row by row.
   if (predicates.size() == 1) {
      return candidates;
   }
   const auto remaining_predicates =
      std::ranges::subrange(predicates.begin() + 1, predicates.end());
   return roaring_util::CopyOnWriteContainer{
      filterRows(candidates.view(), remaining_predicates, chunk_id)
   };
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
