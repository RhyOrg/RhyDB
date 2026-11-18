#include "rhydb/query_engine/filter/operators/union.h"

#include <string>
#include <utility>
#include <vector>

#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/string_utils.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::filter::operators {

Union::Union(OperatorVector&& children, storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)),
      children(std::move(children)) {}

Union::~Union() noexcept = default;

std::string Union::toString() const {
   std::string res = "(";
   res += joinWithLimit(children, " | ");
   res += ')';
   return res;
}

Type Union::type() const {
   return UNION;
}

roaring_util::CopyOnWriteContainer Union::evaluateChunk(uint16_t chunk_id) const {
   EVOBENCH_SCOPE("Union", "evaluateChunk");
   const uint32_t chunk_size = row_layout.chunkSize(chunk_id);
   std::vector<roaring_util::CopyOnWriteContainer> child_results;
   child_results.reserve(children.size());
   for (const auto& child : children) {
      auto child_result = child->evaluateChunk(chunk_id);
      if (child_result.getCardinality() == chunk_size) {
         // The remaining children cannot add anything
         return child_result;
      }
      if (!child_result.empty()) {
         child_results.push_back(std::move(child_result));
      }
   }
   if (child_results.empty()) {
      return {};
   }
   if (child_results.size() == 1) {
      return std::move(child_results.front());
   }
   std::vector<roaring_util::RoaringContainerView> child_views;
   child_views.reserve(child_results.size());
   for (const auto& child_result : child_results) {
      child_views.push_back(child_result.view());
   }
   return roaring_util::CopyOnWriteContainer{roaring_util::fastUnion(child_views)};
}

std::unique_ptr<Operator> Union::negate(std::unique_ptr<Union>&& union_operator) {
   auto row_layout = union_operator->row_layout;
   return std::make_unique<Complement>(std::move(union_operator), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
