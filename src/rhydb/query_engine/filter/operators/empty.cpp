#include "rhydb/query_engine/filter/operators/empty.h"

#include <string>
#include <utility>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::filter::operators {

Empty::Empty(storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)) {};

Empty::~Empty() noexcept = default;

std::string Empty::toString() const {
   return "Empty";
}

Type Empty::type() const {
   return EMPTY;
}

roaring_util::CopyOnWriteContainer Empty::evaluateChunk(uint16_t /*chunk_id*/) const {
   return {};
}

std::unique_ptr<Operator> Empty::negate(std::unique_ptr<Empty>&& empty) {
   return std::make_unique<Full>(std::move(empty->row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
