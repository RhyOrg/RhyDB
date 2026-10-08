#include "rhydb/query_engine/filter/operators/threshold.h"

#include <string>
#include <utility>
#include <vector>

#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/query_compilation_exception.h"

namespace rhydb::query_engine::filter::operators {

Threshold::Threshold(
   OperatorVector&& non_negated_children,
   OperatorVector&& negated_children,
   uint32_t number_of_matchers,
   bool match_exactly,
   storage::column::RowLayout row_layout
)
    : non_negated_children(std::move(non_negated_children)),
      negated_children(std::move(negated_children)),
      number_of_matchers(number_of_matchers),
      match_exactly(match_exactly),
      row_layout(std::move(row_layout)) {
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

Bitmap Threshold::evaluate() const {
   EVOBENCH_SCOPE("Threshold", "evaluate");
   uint32_t dp_table_size;
   if (this->match_exactly) {
      // We need to keep track of the ones that matched too many
      dp_table_size = number_of_matchers + 1;
   } else {
      dp_table_size = number_of_matchers;
   }
   std::vector<roaring::Roaring> bitmaps(dp_table_size);
   if (non_negated_children.empty()) {
      bitmaps[0] = negated_children[0]->evaluate().toRoaring();
   } else {
      bitmaps[0] = non_negated_children[0]->evaluate().toRoaring();
   }

   if (non_negated_children.empty()) {
      row_layout.complementInPlace(bitmaps[0]);
   }

   // NOLINTBEGIN(readability-identifier-length)
   const int max_table_index = static_cast<int>(dp_table_size - 1);
   const int non_negated_child_count = static_cast<int>(non_negated_children.size());
   const int negated_child_count = static_cast<int>(negated_children.size());
   const int n = static_cast<int>(number_of_matchers);  // The threshold
   const int k = static_cast<int>(
      non_negated_children.size() + negated_children.size()
   );  // Number of loop iterations

   for (int i = 1; i < non_negated_child_count; ++i) {
      const roaring::Roaring bitmap = non_negated_children[i]->evaluate().toRoaring();
      // positions higher than (i-1) cannot have been reached yet, are therefore all 0s and the
      // conjunction would return 0
      // positions lower than n - k + i - 1 are unable to affect the result, because only (k - i)
      // iterations are left
      for (int j = std::min(max_table_index, i); j > std::max(0, n - k + i - 1); --j) {
         bitmaps[j] |= bitmaps[j - 1] & bitmap;
      }
      if (k - i > n - 1) {
         bitmaps[0] |= bitmap;
      }
   }

   // Only now iterate over negated children.
   // We need to skip the first element, if it is already taken as start-point.
   // We change the operator from 'and' to 'and_not' for the propagation and update the 0th bitmap
   // with the inverse of the negated bitmap
   // We hope the case of flipping does not occur, as 'k - i' might always be '< n - 1'
   // (Number of children left is less than the distance we need to cross to reach the result)
   const int took_first_offset = non_negated_children.empty() ? 1 : 0;
   for (int local_i = took_first_offset; local_i < negated_child_count; ++local_i) {
      roaring::Roaring bitmap = negated_children[local_i]->evaluate().toRoaring();
      const int i = local_i + non_negated_child_count;
      // positions higher than (i-1) cannot have been reached yet, are therefore all 0s and the
      // conjunction would return 0
      // positions lower than (n-1) - (k-i) are unable to affect the result, because only (k-i)
      // iterations are left
      for (int j = std::min(max_table_index, i); j > std::max(0, n - k + i - 1); --j) {
         bitmaps[j] |= bitmaps[j - 1] - bitmap;
      }
      if (k - i > n - 1) {
         row_layout.complementInPlace(bitmap);
         bitmaps[0] |= bitmap;
      }
   }
   // NOLINTEND(readability-identifier-length)

   if (this->match_exactly) {
      // Because exact, we remove all that have too many
      bitmaps[number_of_matchers - 1] -= bitmaps[number_of_matchers];

      return Bitmap(std::move(bitmaps[number_of_matchers - 1]));
   }
   return Bitmap(std::move(bitmaps.back()));
}

std::unique_ptr<Operator> Threshold::negate(std::unique_ptr<Threshold>&& threshold) {
   auto row_layout = threshold->row_layout;
   return std::make_unique<Complement>(std::move(threshold), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
