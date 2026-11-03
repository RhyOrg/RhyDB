#pragma once

#include <memory>
#include <string>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::filter::operators {

class Full : public Operator {
  public:
   explicit Full(storage::column::RowLayout row_layout);

   ~Full() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Full>&& full_operator);
};

}  // namespace rhydb::query_engine::filter::operators
