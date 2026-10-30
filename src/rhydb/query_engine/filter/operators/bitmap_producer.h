#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::filter::operators {

class BitmapProducer : public Operator {
  private:
   std::function<Bitmap()> producer;
   // The producer computes the rows of all chunks at once, so its result is computed on the first
   // evaluated chunk and kept for the others
   mutable std::once_flag produced;
   mutable Bitmap produced_bitmap;

  public:
   explicit BitmapProducer(std::function<Bitmap()> producer, storage::column::RowLayout row_layout);

   ~BitmapProducer() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<BitmapProducer>&& bitmap_producer);
};

}  // namespace rhydb::query_engine::filter::operators
