#include "rhydb/query_engine/filter/operators/range_selection.h"

#include <roaring/roaring.hh>
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
    : ranges(std::move(ranges)),
      row_layout(std::move(row_layout)) {}

RangeSelection::~RangeSelection() noexcept = default;

std::string RangeSelection::toString() const {
   std::vector<std::string> range_strings;
   std::ranges::transform(
      ranges,
      std::back_inserter(range_strings),
      [](const RangeSelection::Range& range) {
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

Bitmap RangeSelection::evaluate() const {
   EVOBENCH_SCOPE("RangeSelection", "evaluate");
   roaring::Roaring result_bitmap;
   for (const auto& [start, end] : ranges) {
      RHYDB_ASSERT(
         (end.chunk_id < row_layout.numChunks() &&
          end.row_in_chunk < row_layout.chunkSize(end.chunk_id)) ||
         (end.chunk_id == row_layout.numChunks() && end.row_in_chunk == 0)
      );
      if (start.chunk_id == end.chunk_id) {
         result_bitmap.addRange(start.toGlobal(), end.toGlobal());
      } else {
         // Add all chunks [start.chunk_id, ..., end.chunk_id]. The first and last possibly partial

         auto end_of_start_chunk =
            RowId::chunkStart(start.chunk_id) + row_layout.chunkSize(start.chunk_id);
         if (start.toGlobal() != end_of_start_chunk) {
            result_bitmap.addRange(start.toGlobal(), end_of_start_chunk);
         }

         for (uint16_t chunk_id = start.chunk_id + 1; chunk_id < end.chunk_id; chunk_id++) {
            auto chunk_start = RowId::chunkStart(chunk_id);
            auto chunk_end = RowId::chunkStart(chunk_id) + row_layout.chunkSize(chunk_id);
            result_bitmap.addRange(chunk_start, chunk_end);
         }

         auto start_of_end_chunk = RowId{.chunk_id = end.chunk_id, .row_in_chunk = 0};
         if (end != start_of_end_chunk) {
            result_bitmap.addRange(start_of_end_chunk.toGlobal(), end.toGlobal());
         }
      }
   }
   return Bitmap{std::move(result_bitmap)};
}

std::unique_ptr<Operator> RangeSelection::negate(std::unique_ptr<RangeSelection>&& range_selection
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
      .chunk_id = static_cast<uint16_t>(range_selection->row_layout.numChunks()), .row_in_chunk = 0,
   };
   if (last_end != ranges_end) {
      new_ranges.emplace_back(last_end, ranges_end);
   }
   return std::make_unique<RangeSelection>(
      std::move(new_ranges), std::move(range_selection->row_layout)
   );
}

}  // namespace rhydb::query_engine::filter::operators
