#include "rhydb/query_engine/filter/operators/is_in_covered_region.h"

#include <memory>
#include <string>

#include <fmt/format.h>

#include "evobench/evobench.hpp"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

namespace rhydb::query_engine::filter::operators {

IsInCoveredRegion::IsInCoveredRegion(
   const Chunks* chunks,
   uint32_t position_idx,
   Comparator comparator
)
    : chunks(chunks),
      position_idx(position_idx),
      comparator(comparator) {}

IsInCoveredRegion::~IsInCoveredRegion() noexcept = default;

std::string IsInCoveredRegion::toString() const {
   return fmt::format(
      "{}IsInCoveredRegion({})", comparator == Comparator::IS_COVERED ? "" : "!", position_idx
   );
}

bool IsInCoveredRegion::isCovered(storage::column::RowId row_id) const {
   return chunks->at(row_id.chunk_id)->coverage().covers(row_id.row_in_chunk, position_idx);
}

bool IsInCoveredRegion::match(storage::column::RowId row_id) const {
   return isCovered(row_id) == (comparator == Comparator::IS_COVERED);
}

roaring_util::RoaringContainer IsInCoveredRegion::makeChunk(
   uint16_t chunk_id,
   uint32_t chunk_size
) const {
   EVOBENCH_SCOPE("IsInCoveredRegion", "makeChunk");
   auto covered_rows = storage::sequence::coveredRows(*chunks->at(chunk_id), position_idx);
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
   return std::make_unique<operators::IsInCoveredRegion>(chunks, position_idx, comparator);
}

std::unique_ptr<Predicate> IsInCoveredRegion::negate() const {
   const Comparator negated_comparator = comparator == Comparator::IS_COVERED
                                            ? IsInCoveredRegion::Comparator::IS_NOT_COVERED
                                            : IsInCoveredRegion::Comparator::IS_COVERED;
   return std::make_unique<operators::IsInCoveredRegion>(chunks, position_idx, negated_comparator);
}

}  // namespace rhydb::query_engine::filter::operators
