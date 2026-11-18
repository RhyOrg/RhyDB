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
    : Operator(std::move(row_layout)),
      children(std::move(children)),
      negated_children(std::move(negated_children)) {
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

roaring_util::CopyOnWriteContainer Intersection::evaluateChunk(uint16_t chunk_id) const {
   EVOBENCH_SCOPE("Intersection", "evaluateChunk");
   std::vector<roaring_util::CopyOnWriteContainer> child_results;
   child_results.reserve(children.size());
   for (const auto& child : children) {
      auto child_result = child->evaluateChunk(chunk_id);
      if (child_result.empty()) {
         // The remaining children cannot select anything
         return {};
      }
      child_results.push_back(std::move(child_result));
   }
   // Intersect the smallest first, such that intermediate results are kept small
   std::ranges::sort(child_results, {}, &roaring_util::CopyOnWriteContainer::getCardinality);

   // children is not empty, as asserted in the constructor
   roaring_util::CopyOnWriteContainer result = std::move(child_results.front());
   for (size_t idx = 1; idx < child_results.size() && !result.empty(); ++idx) {
      result &= child_results[idx].view();
   }
   for (const auto& negated_child : negated_children) {
      if (result.empty()) {
         break;
      }
      result -= negated_child->evaluateChunk(chunk_id).view();
   }
   return result;
}

std::unique_ptr<Operator> Intersection::negate(std::unique_ptr<Intersection>&& intersection) {
   auto row_layout = intersection->row_layout;
   return std::make_unique<Complement>(std::move(intersection), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
