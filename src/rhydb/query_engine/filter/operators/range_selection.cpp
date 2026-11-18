#include "rhydb/query_engine/filter/operators/range_selection.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <boost/algorithm/string/join.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::filter::operators {

using storage::column::RowId;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
RangeSelection::Range::Range(RowId start, RowId end)
    : start(start),
      end(end) {}

RangeSelection::RangeSelection(std::vector<Range>&& ranges, storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)),
      ranges(std::move(ranges)) {
   for (const auto& range : this->ranges) {
      RHYDB_ASSERT(
         (range.end.chunk_id < this->row_layout.numChunks() &&
          range.end.row_in_chunk < this->row_layout.chunkSize(range.end.chunk_id)) ||
         (range.end.chunk_id == this->row_layout.numChunks() && range.end.row_in_chunk == 0)
      );
   }
}

RangeSelection::~RangeSelection() noexcept = default;

std::string RangeSelection::toString() const {
   std::vector<std::string> range_strings;
   std::ranges::transform(
      ranges, std::back_inserter(range_strings), [](const RangeSelection::Range& range) {
         return fmt::format(
            "({},{})-({},{})",
            range.start.chunk_id,
            range.start.row_in_chunk,
            range.end.chunk_id,
            range.end.row_in_chunk
         );
      }
   );
   return "RangeSelection(" + boost::algorithm::join(range_strings, ", ") + ")";
}

Type RangeSelection::type() const {
   return RANGE_SELECTION;
}

roaring_util::CopyOnWriteContainer RangeSelection::evaluateChunk(uint16_t chunk_id) const {
   EVOBENCH_SCOPE("RangeSelection", "evaluateChunk");
   // The global row ids of the chunk's rows. A range spanning several chunks also spans the unused
   // row ids between them, which this intersection drops.
   const uint32_t chunk_start = RowId::chunkStart(chunk_id);
   const uint32_t chunk_end = chunk_start + row_layout.chunkSize(chunk_id);
   roaring_util::CopyOnWriteContainer result;
   for (const auto& [start, end] : ranges) {
      const uint32_t range_start = std::max(start.toGlobal(), chunk_start);
      const uint32_t range_end = std::min(end.toGlobal(), chunk_end);
      if (range_start < range_end) {
         result |= roaring_util::RoaringContainerView{roaring_util::RoaringContainer::ofRange(
            range_start - chunk_start, range_end - chunk_start
         )};
      }
   }
   return result;
}

std::unique_ptr<Operator> RangeSelection::negate(
   std::unique_ptr<RangeSelection>&& range_selection
) {
   std::vector<Range> new_ranges;
   // Guard against empty case
   if (range_selection->row_layout.numChunks() == 0) {
      return std::make_unique<RangeSelection>(
         std::move(new_ranges), std::move(range_selection->row_layout)
      );
   }
   RowId last_end = *range_selection->row_layout.begin();
   for (const auto& current : range_selection->ranges) {
      if (last_end != current.start) {
         new_ranges.emplace_back(last_end, current.start);
      }
      last_end = current.end;
   }
   const RowId ranges_end{
      .chunk_id = static_cast<uint16_t>(range_selection->row_layout.numChunks()),
      .row_in_chunk = 0,
   };
   if (last_end != ranges_end) {
      new_ranges.emplace_back(last_end, ranges_end);
   }
   return std::make_unique<RangeSelection>(
      std::move(new_ranges), std::move(range_selection->row_layout)
   );
}

}  // namespace rhydb::query_engine::filter::operators
