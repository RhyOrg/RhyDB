#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::filter::operators {

class RangeSelection : public Operator {
  public:
   struct Range {
      storage::column::RowId start;
      storage::column::RowId end;

      explicit Range(storage::column::RowId start, storage::column::RowId end);
   };

  private:
   std::vector<Range> ranges;

  public:
   explicit RangeSelection(std::vector<Range>&& ranges, storage::column::RowLayout row_layout);

   ~RangeSelection() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<RangeSelection>&& range_selection);
};

}  // namespace rhydb::query_engine::filter::operators
