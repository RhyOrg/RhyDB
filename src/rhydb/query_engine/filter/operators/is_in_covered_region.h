#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/column/horizontal_coverage_index.h"

namespace rhydb::query_engine::filter::operators {

class IsInCoveredRegion : public Predicate {
  public:
   enum class Comparator : uint8_t { IS_COVERED, IS_NOT_COVERED };

  private:
   const storage::column::HorizontalCoverageIndex* horizontal_coverage_index;
   uint32_t position_idx;
   Comparator comparator;

  public:
   explicit IsInCoveredRegion(
      const storage::column::HorizontalCoverageIndex* horizontal_coverage_index,
      uint32_t position_idx,
      Comparator comparator
   );

   ~IsInCoveredRegion() noexcept override;

   [[nodiscard]] std::string toString() const override;
   [[nodiscard]] bool isCovered(uint32_t row_id) const;
   [[nodiscard]] bool match(storage::column::RowId row_id) const override;
   [[nodiscard]] roaring::Roaring makeBitmap(
      const storage::column::RowLayout& row_layout
   ) const override;
   [[nodiscard]] double estimateSelectivity(uint32_t row_count) const override;

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override;
   [[nodiscard]] std::unique_ptr<Predicate> negate() const override;
};

}  // namespace rhydb::query_engine::filter::operators
