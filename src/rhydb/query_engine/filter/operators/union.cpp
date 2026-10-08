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
    : children(std::move(children)),
      row_layout(std::move(row_layout)) {}

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

Bitmap Union::evaluate() const {
   EVOBENCH_SCOPE("Union", "evaluate");
   std::vector<Bitmap> child_res;
   child_res.reserve(children.size());
   for (const auto& child : children) {
      child_res.push_back(child->evaluate());
   }
   return Bitmap::fastUnion(child_res);
}

std::unique_ptr<Operator> Union::negate(std::unique_ptr<Union>&& union_operator) {
   auto row_layout = union_operator->row_layout;
   return std::make_unique<Complement>(std::move(union_operator), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
