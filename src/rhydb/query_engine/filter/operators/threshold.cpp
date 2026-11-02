#include "rhydb/query_engine/filter/operators/threshold.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::query_engine::filter::operators {

Threshold::Threshold(
   OperatorVector&& non_negated_children,
   OperatorVector&& negated_children,
   uint32_t number_of_matchers,
   bool match_exactly,
   storage::column::RowLayout row_layout
)
    : Operator(std::move(row_layout)),
      non_negated_children(std::move(non_negated_children)),
      negated_children(std::move(negated_children)),
      number_of_matchers(number_of_matchers),
      match_exactly(match_exactly) {
   if (number_of_matchers >= this->non_negated_children.size() + this->negated_children.size()) {
      throw QueryCompilationException(
         "Compilation Error: number_of_matchers must be less than the number of children of a "
         "threshold expression"
      );
   }
   if (number_of_matchers == 0) {
      throw QueryCompilationException(
         "Compilation Error: number_of_matchers must be greater than zero"
      );
   }
}

Threshold::~Threshold() noexcept = default;

std::string Threshold::toString() const {
   std::string res = "Threshold(";
   if (match_exactly) {
      res += '=';
   } else {
      res += ">=";
   }
   res += fmt::format("{}-of ", number_of_matchers);

   res += "non_negated: (" + joinWithLimit(non_negated_children) + ") ";
   res += "negated: (" + joinWithLimit(negated_children) + ") ";
   res += ')';
   return res;
}

Type Threshold::type() const {
   return THRESHOLD;
}

namespace {

using roaring_util::CopyOnWriteContainer;
using roaring_util::RoaringContainer;
using roaring_util::RoaringContainerView;

/// The table of the threshold's dynamic program over one chunk: after adding the first `i + 1`
/// children, entry `j` holds the rows that at least `j + 1` of them match, for the entries that can
/// still affect the result. Entries above `highest_reached` are empty, as no row has been matched
/// that often yet. When the children are sparse, most entries stay empty, so they are skipped
/// instead of combining empty containers.
class MatchCounts {
   std::vector<CopyOnWriteContainer> entries;
   int highest_reached = -1;
   int number_of_matchers;
   int child_count;
   uint32_t chunk_size;

   void markReached(int entry) {
      if (!entries[entry].empty()) {
         highest_reached = std::max(highest_reached, entry);
      }
   }

   void addRows(int entry, RoaringContainer rows) {
      if (entries[entry].empty()) {
         entries[entry] = CopyOnWriteContainer{std::move(rows)};
      } else if (!rows.empty()) {
         entries[entry] |= RoaringContainerView{rows};
      }
      markReached(entry);
   }

   // NOLINTBEGIN(readability-identifier-length)
   /// The entries to update when adding the `i`-th child, from `highestEntry(i)` down to (but
   /// excluding) `lowestEntry(i)`:
   /// positions higher than (i-1) cannot have been reached yet, are therefore all 0s and the
   /// conjunction would return 0
   /// positions lower than (n-1) - (k-i) are unable to affect the result, because only (k-i)
   /// iterations are left
   [[nodiscard]] int highestEntry(int i) const {
      return std::min({static_cast<int>(entries.size()) - 1, i, highest_reached + 1});
   }

   [[nodiscard]] int lowestEntry(int i) const {
      return std::max(0, number_of_matchers - child_count + i - 1);
   }

   /// Whether entry 0 can still affect the result, as `k - i` children are left
   [[nodiscard]] bool firstEntryMatters(int i) const {
      return child_count - i > number_of_matchers - 1;
   }

  public:
   MatchCounts(
      CopyOnWriteContainer first_child_rows,
      size_t table_size,
      uint32_t number_of_matchers,
      size_t child_count,
      uint32_t chunk_size
   )
       : entries(table_size),
         number_of_matchers(static_cast<int>(number_of_matchers)),
         child_count(static_cast<int>(child_count)),
         chunk_size(chunk_size) {
      entries[0] = std::move(first_child_rows);
      markReached(0);
   }

   /// Adds the `i`-th child, which matches `child`
   void addChild(int i, const CopyOnWriteContainer& child) {
      if (child.empty()) {
         return;
      }
      for (int j = highestEntry(i); j > lowestEntry(i); --j) {
         if (!entries[j - 1].empty()) {
            addRows(j, entries[j - 1].view() & child.view());
         }
      }
      if (firstEntryMatters(i)) {
         entries[0] |= child.view();
         markReached(0);
      }
   }

   /// Adds the `i`-th child, which matches the rows that are not in `child`
   void addNegatedChild(int i, const CopyOnWriteContainer& child) {
      for (int j = highestEntry(i); j > lowestEntry(i); --j) {
         if (entries[j - 1].empty()) {
            continue;
         }
         if (child.empty()) {
            // The negated child matches every row of `entries[j - 1]`
            const CopyOnWriteContainer previous = entries[j - 1];
            if (entries[j].empty()) {
               entries[j] = previous;
            } else {
               entries[j] |= previous.view();
            }
            markReached(j);
         } else {
            addRows(j, entries[j - 1].view() - child.view());
         }
      }
      if (firstEntryMatters(i)) {
         entries[0] |= RoaringContainerView{roaring_util::complement(child.view(), chunk_size)};
         markReached(0);
      }
   }
   // NOLINTEND(readability-identifier-length)

   /// The rows that at least `count` children match
   [[nodiscard]] CopyOnWriteContainer& atLeast(uint32_t count) { return entries[count - 1]; }
};

}  // namespace

roaring_util::CopyOnWriteContainer Threshold::evaluateChunk(uint16_t chunk_id) const {
   EVOBENCH_SCOPE("Threshold", "evaluateChunk");
   const uint32_t chunk_size = row_layout.chunkSize(chunk_id);
   // When matching exactly, we need to keep track of the ones that matched too many
   const size_t table_size = match_exactly ? number_of_matchers + 1 : number_of_matchers;
   const size_t child_count = non_negated_children.size() + negated_children.size();

   CopyOnWriteContainer first_child_rows;
   if (non_negated_children.empty()) {
      first_child_rows = CopyOnWriteContainer{
         roaring_util::complement(negated_children[0]->evaluateChunk(chunk_id).view(), chunk_size)
      };
   } else {
      first_child_rows = non_negated_children[0]->evaluateChunk(chunk_id);
   }
   MatchCounts counts{
      std::move(first_child_rows), table_size, number_of_matchers, child_count, chunk_size
   };

   const int non_negated_child_count = static_cast<int>(non_negated_children.size());
   for (int idx = 1; idx < non_negated_child_count; ++idx) {
      counts.addChild(idx, non_negated_children[idx]->evaluateChunk(chunk_id));
   }

   // Only now iterate over negated children.
   // We need to skip the first element, if it is already taken as start-point.
   // We change the operator from 'and' to 'and_not' for the propagation and update the 0th bitmap
   // with the inverse of the negated bitmap
   // We hope the case of flipping does not occur, as 'k - i' might always be '< n - 1'
   // (Number of children left is less than the distance we need to cross to reach the result)
   const int took_first_offset = non_negated_children.empty() ? 1 : 0;
   const int negated_child_count = static_cast<int>(negated_children.size());
   for (int local_idx = took_first_offset; local_idx < negated_child_count; ++local_idx) {
      counts.addNegatedChild(
         local_idx + non_negated_child_count, negated_children[local_idx]->evaluateChunk(chunk_id)
      );
   }

   if (match_exactly) {
      // Because exact, we remove all that have too many
      counts.atLeast(number_of_matchers) -= counts.atLeast(number_of_matchers + 1).view();
   }
   return std::move(counts.atLeast(number_of_matchers));
}

std::unique_ptr<Operator> Threshold::negate(std::unique_ptr<Threshold>&& threshold) {
   auto row_layout = threshold->row_layout;
   return std::make_unique<Complement>(std::move(threshold), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
