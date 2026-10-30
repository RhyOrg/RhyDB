#include "rhydb/query_engine/filter/operators/full.h"

#include <string>
#include <utility>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::filter::operators {

Full::Full(storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)) {}

Full::~Full() noexcept = default;

std::string Full::toString() const {
   return "Full";
}

Type Full::type() const {
   return FULL;
}

roaring_util::CopyOnWriteContainer Full::evaluateChunk(uint16_t chunk_id) const {
   return roaring_util::CopyOnWriteContainer{
      roaring_util::RoaringContainer::ofRange(0, row_layout.chunkSize(chunk_id))
   };
}

std::unique_ptr<Operator> Full::negate(std::unique_ptr<Full>&& full) {
   return std::make_unique<Empty>(std::move(full->row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
