#include "rhydb/query_engine/filter/operators/is_in_covered_region.h"

#include <memory>
#include <string>

#include <fmt/format.h>
#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/query_engine/filter/operators/selection.h"

namespace rhydb::query_engine::filter::operators {

IsInCoveredRegion::IsInCoveredRegion(
   const storage::column::HorizontalCoverageIndex* horizontal_coverage_index,
   uint32_t position_idx,
   Comparator comparator
)
    : horizontal_coverage_index(horizontal_coverage_index),
      position_idx(position_idx),
      comparator(comparator) {}

IsInCoveredRegion::~IsInCoveredRegion() noexcept = default;

std::string IsInCoveredRegion::toString() const {
   return fmt::format(
      "{}IsInCoveredRegion({})", comparator == Comparator::IS_COVERED ? "" : "!", position_idx
   );
}

bool IsInCoveredRegion::isCovered(uint32_t row_id) const {
   const auto [start, end] = horizontal_coverage_index->coverageRange(row_id);
   // Check whether `value` is covered -> value in [start, end) and not in row_bitmap

   // Is it outside the range?
   if (position_idx < start || position_idx >= end) {
      return false;
   }

   if (
      auto row_bitmap = horizontal_coverage_index->horizontal_bitmaps.find(row_id);
      row_bitmap != horizontal_coverage_index->horizontal_bitmaps.end()
   ) {
      const bool is_covered = !row_bitmap->second.contains(position_idx);
      return is_covered;
   }
   // If no bitmap is there, the entire range is covered
   return true;
}

bool IsInCoveredRegion::match(storage::column::RowId row_id) const {
   return isCovered(row_id.toGlobal()) == (comparator == Comparator::IS_COVERED);
}

roaring_util::RoaringContainer IsInCoveredRegion::makeChunk(
   uint16_t chunk_id,
   uint32_t chunk_size
) const {
   EVOBENCH_SCOPE("IsInCoveredRegion", "makeChunk");
   auto covered_rows = horizontal_coverage_index->coveredRowsInChunk(position_idx, chunk_id);
   if (comparator == Comparator::IS_NOT_COVERED) {
      return roaring_util::complement(roaring_util::RoaringContainerView{covered_rows}, chunk_size);
   }
   return covered_rows;
}

double IsInCoveredRegion::estimateSelectivity(uint32_t /*row_count*/) const {
   EVOBENCH_SCOPE("IsInCoveredRegion", "estimateSelectivity");
   return 0.1;
}

std::unique_ptr<Predicate> IsInCoveredRegion::copy() const {
   return std::make_unique<operators::IsInCoveredRegion>(
      horizontal_coverage_index, position_idx, comparator
   );
}

std::unique_ptr<Predicate> IsInCoveredRegion::negate() const {
   const Comparator negated_comparator = comparator == Comparator::IS_COVERED
                                            ? IsInCoveredRegion::Comparator::IS_NOT_COVERED
                                            : IsInCoveredRegion::Comparator::IS_COVERED;
   return std::make_unique<operators::IsInCoveredRegion>(
      horizontal_coverage_index, position_idx, negated_comparator
   );
}

}  // namespace rhydb::query_engine::filter::operators
