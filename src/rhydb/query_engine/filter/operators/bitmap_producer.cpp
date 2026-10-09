#include "rhydb/query_engine/filter/operators/bitmap_producer.h"

#include <utility>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::filter::operators {

BitmapProducer::BitmapProducer(
   std::function<Bitmap()> producer,
   storage::column::RowLayout row_layout
)
    : producer(std::move(producer)),
      row_layout(std::move(row_layout)) {}

BitmapProducer::~BitmapProducer() noexcept = default;

std::string BitmapProducer::toString() const {
   return "BitmapProducer";
}

Type BitmapProducer::type() const {
   return BITMAP_PRODUCER;
}

Bitmap BitmapProducer::evaluate() const {
   EVOBENCH_SCOPE("BitmapProducer", "evaluate");
   return producer();
}

std::unique_ptr<Operator> BitmapProducer::negate(
   std::unique_ptr<BitmapProducer>&& bitmap_producer
) {
   auto row_layout = bitmap_producer->row_layout;
   return std::make_unique<Complement>(std::move(bitmap_producer), std::move(row_layout));
}

}  // namespace rhydb::query_engine::filter::operators
