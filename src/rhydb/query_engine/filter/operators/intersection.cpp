#include "rhydb/query_engine/filter/operators/intersection.h"

#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/query_compilation_exception.h"

namespace rhydb::query_engine::filter::operators {

Intersection::Intersection(
   OperatorVector&& children,
   OperatorVector&& negated_children,
   storage::column::RowLayout row_layout
)
    : children(std::move(children)),
      negated_children(std::move(negated_children)),
      row_layout(std::move(row_layout)) {
   if (this->children.empty()) {
      SPDLOG_ERROR(
         "Compilation bug: Intersection without non-negated children is not allowed. "
         "Should be compiled as a union."
      );
      throw QueryCompilationException(
         "Compilation bug: Intersection without non-negated children is not allowed. "
         "Should be compiled as a union."
      );
   }
   if (this->children.size() + this->negated_children.size() < 2) {
      SPDLOG_ERROR("Compilation bug: Intersection needs at least two children.");

      throw QueryCompilationException("Compilation bug: Intersection needs at least two children.");
   }
}

std::unique_ptr<Operator> Intersection::ofComplements(
   OperatorVector children,
   storage::column::RowLayout row_layout
) {
   if (children.empty()) {
      return std::make_unique<Full>(std::move(row_layout));
   }
   if (children.size() == 1) {
      return std::make_unique<Complement>(std::move(children.front()), std::move(row_layout));
   }
   // The negated children are subtracted from the non-negated ones, so negating a single
   // child yields the set the remaining ones can be subtracted from.
   OperatorVector non_negated;
   non_negated.push_back(std::make_unique<Complement>(std::move(children.front()), row_layout));
   OperatorVector negated;
   negated.reserve(children.size() - 1);
   std::move(std::next(children.begin()), children.end(), std::back_inserter(negated));
   return std::make_unique<Intersection>(
      std::move(non_negated), std::move(negated), std::move(row_layout)
   );
}

Intersection::~Intersection() noexcept = default;

std::string Intersection::toString() const {
   std::string res = "Intersection(";

   res += "non_negated: (" + joinWithLimit(children) + ") ";
   res += "negated: (" + joinWithLimit(negated_children) + ") ";

   res += ')';
   return res;
}

Type Intersection::type() const {
   return INTERSECTION;
}

Bitmap Intersection::evaluate() const {
   EVOBENCH_SCOPE("Intersection", "evaluate");
   std::vector<Bitmap> children_bm;
   children_bm.reserve(children.size());
   std::ranges::transform(children, std::back_inserter(children_bm), [&](const auto& child) {
      return child->evaluate();
   });
   std::vector<Bitmap> negated_children_bm;
   negated_children_bm.reserve(negated_children.size());
   std::ranges::transform(
      negated_children, std::back_inserter(negated_children_bm), [&](const auto& child) {
         return child->evaluate();
      }
   );
   // Sort ascending, such that intermediate results are kept small
   std::ranges::sort(children_bm, [](const Bitmap& expression1, const Bitmap& expression2) {
      return expression1.cardinality() < expression2.cardinality();
   });
   // Sort negated children descending by size
   std::ranges::sort(
      negated_children_bm, [](const Bitmap& expression_result1, const Bitmap& expression_result2) {
         return expression_result1.cardinality() > expression_result2.cardinality();
      }
   );

   // children_bm > 0 as asserted in constructor
   if (children_bm.size() == 1) {
      // negated_children_bm cannot be empty because of size assertion in constructor
      Bitmap result = std::move(children_bm[0]);
      for (auto& neg_bm : negated_children_bm) {
         result -= neg_bm;
      }
      return result;
   }
   Bitmap result = std::move(children_bm[0]);
   result &= children_bm[1];
   for (uint32_t i = 2; i < children_bm.size(); i++) {
      result &= children_bm[i];
   }
   for (auto& neg_bm : negated_children_bm) {
      result -= neg_bm;
   }
   return result;
}

std::unique_ptr<Operator> Intersection::negate(std::unique_ptr<Intersection>&& intersection) {
   auto row_layout = intersection->row_layout;
   return std::make_unique<Complement>(std::move(intersection), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
