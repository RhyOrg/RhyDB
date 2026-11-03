#include "rhydb/query_engine/filter/operators/index_scan.h"

#include <string>

#include <fmt/format.h>
#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::filter::operators {

IndexScan::IndexScan(Bitmap bitmap, storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)),
      bitmap(std::move(bitmap)) {}

IndexScan::IndexScan(
   std::unique_ptr<scalar_expressions::ScalarExpression>&& logical_equivalent,
   Bitmap bitmap,
   storage::column::RowLayout row_layout
)
    : Operator(std::move(row_layout)),
      logical_equivalent(std::move(logical_equivalent)),
      bitmap(std::move(bitmap)) {}

IndexScan::~IndexScan() noexcept = default;

std::string IndexScan::toString() const {
   return fmt::format(
      "IndexScan(Logical Equivalent: {}, Cardinality: {})",
      logical_equivalent ? logical_equivalent.value()->toString() : "undefined",
      std::to_string(bitmap.cardinality())
   );
}

Type IndexScan::type() const {
   return INDEX_SCAN;
}

roaring_util::CopyOnWriteContainer IndexScan::evaluateChunk(uint16_t chunk_id) const {
   // A copy, so that a container owned by `bitmap` is cloned instead of borrowed
   return bitmap.containerAt(chunk_id);
}
std::unique_ptr<Operator> IndexScan::negate(std::unique_ptr<IndexScan>&& index_scan) {
   auto row_layout = index_scan->row_layout;
   return std::make_unique<Complement>(std::move(index_scan), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
