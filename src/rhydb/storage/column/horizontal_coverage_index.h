#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/roaring_util/bitmap_builder.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/row_id.h"

// AVX-512 is x86-only; the project also targets Apple-Silicon macOS and wasm, where
// <immintrin.h>, the `target` attribute and `__builtin_cpu_supports` do not exist. The guard lives
// here rather than in the .cpp so that tests can see which implementations this build contains.
#if defined(__x86_64__) || defined(_M_X64)
#define RHYDB_HAS_X86_SIMD 1
#endif

namespace rhydb {
class Coverage;
}

namespace rhydb::storage::column {

class HorizontalCoverageIndex {
  public:
   /// Per-row N positions inside the covered region, keyed by sparse global row id (chunk `k` lives
   /// at `k << 16`, see `RowId`). Only rows that actually carry such positions get an entry.
   std::map<uint32_t, roaring::Roaring> horizontal_bitmaps;

   /// Per-row covered `[start, end)` range, held as a struct-of-arrays: `starts[chunk_id]` and
   /// `ends[chunk_id]` are parallel per-chunk arrays, so the row whose global id is
   /// `(chunk_id << 16) | row_in_chunk` has range `[starts[chunk_id][row_in_chunk],
   /// ends[chunk_id][row_in_chunk])`
   std::vector<std::vector<uint32_t>> starts;
   std::vector<std::vector<uint32_t>> ends;

   /// per-2^16-batch statistics for more efficient operations
   std::vector<uint32_t> batch_min_start;
   std::vector<uint32_t> batch_max_start;
   std::vector<uint32_t> batch_min_end;
   std::vector<uint32_t> batch_max_end;

   void insertCoverage(RowId row_id, const Coverage& coverage);

   void insertNullSequence(RowId row_id);

   /// The number of rows covering each position
   [[nodiscard]] std::vector<uint64_t> computeCoverageCardinalities(size_t genome_length) const;

   [[nodiscard]] size_t numChunks() const { return starts.size(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const {
      return static_cast<uint32_t>(starts.at(chunk_id).size());
   }

   /// The covered `[start, end)` range of the row addressed by its sparse global row id.
   [[nodiscard]] std::pair<uint32_t, uint32_t> coverageRange(uint32_t global_row_id) const {
      const RowId row_id = RowId::fromGlobal(global_row_id);
      return {
         starts.at(row_id.chunk_id).at(row_id.row_in_chunk),
         ends.at(row_id.chunk_id).at(row_id.row_in_chunk),
      };
   }

   /// The rows of a single 2^16 chunk that cover `position` (i.e. `position` lies in the row's
   /// `[start, end)` and is not one of the row's in-region N positions). This is the single-chunk
   /// analogue of `getCoverageBitmapForPositions`, kept so the bitmap-aggregation node can compute
   /// per-symbol groups one filter chunk at a time and skip chunks the filter does not touch. Uses
   /// the same envelope fast paths (skip a chunk that cannot cover the position; bulk-add a chunk
   /// that fully covers it) as the batch method. Every matching row lives in the one 2^16 chunk, so
   /// the result is a single roaring container returned directly (empty if no row covers the
   /// position), sparing the caller a `roaring::Roaring` wrapper.
   [[nodiscard]] roaring_util::RoaringContainer coveredRowsInChunk(
      uint32_t position,
      uint16_t chunk_id
   ) const;

   /// True if no row in `chunk_id` covers `position` -- the position lies outside the chunk's
   /// covered envelope (`[batch_min_start, batch_max_end)`), so every row is missing there. O(1);
   /// lets a caller skip building the (empty) covered set for such a chunk.
   [[nodiscard]] bool noRowCoversPositionInChunk(uint32_t position, uint16_t chunk_id) const;

   /// True if *every* row in `chunk_id` covers `position` with no in-region N there. It combines
   /// the covered-range intersection envelope (`[batch_max_start, batch_min_end)`, O(1)) with a
   /// scan of the chunk's in-region-N rows for one carrying an N at `position`. When this holds and
   /// no mutation is recorded at the position, every row carries the reference symbol, so the
   /// caller can treat the whole chunk as one group without materializing the covered set.
   [[nodiscard]] bool positionCoveredByWholeChunk(uint32_t position, uint16_t chunk_id) const;

   template <size_t BatchSize>
   [[nodiscard]] std::array<roaring::Roaring, BatchSize> getCoverageBitmapForPositions(
      uint32_t position
   ) const {
      const uint32_t range_start = position;
      const uint32_t range_end = position + BatchSize;

      using roaring_util::BitmapBuilderByRange;
      // Rows of partially-covered chunks are added one at a time (coalesced into ranges by the
      // builder); fully-covered chunks are bulk-added to `result` directly with `addRange`.
      std::array<BitmapBuilderByRange, BatchSize> partial_builders;
      std::array<roaring::Roaring, BatchSize> result;

      for (size_t chunk_id = 0; chunk_id < starts.size(); ++chunk_id) {
         if (batch_max_end.at(chunk_id) <= range_start ||
             batch_min_start.at(chunk_id) >= range_end) {
            continue;
         }
         const uint32_t base_row_id = static_cast<uint32_t>(chunk_id) << 16;
         const auto& chunk_starts = starts[chunk_id];
         const auto& chunk_ends = ends[chunk_id];

         // Fast path: if the whole batch range lies within the chunk's intersection envelope
         // `[batch_max_start, batch_min_end)`, every row in the chunk covers every position of the
         // batch, so add the entire chunk to each position in one range operation.
         if (batch_max_start.at(chunk_id) <= range_start &&
             range_end <= batch_min_end.at(chunk_id)) {
            for (auto& bitmap : result) {
               bitmap.addRange(base_row_id, base_row_id + chunk_starts.size());
            }
            continue;
         }

         for (size_t row_in_chunk = 0; row_in_chunk < chunk_starts.size(); ++row_in_chunk) {
            const uint32_t row_id = base_row_id | static_cast<uint32_t>(row_in_chunk);
            for (uint32_t pos = std::max(range_start, chunk_starts[row_in_chunk]);
                 pos < std::min(range_end, chunk_ends[row_in_chunk]);
                 ++pos) {
               partial_builders[pos - range_start].add(row_id);
            }
         }
      }

      for (size_t i = 0; i < BatchSize; ++i) {
         result[i] |= std::move(partial_builders[i]).getBitmap();
      }

      roaring::Roaring range_bitmap;
      range_bitmap.addRange(range_start, range_end);
      for (const auto& [sequence_idx, bitmap] : horizontal_bitmaps) {
         auto bitmap_in_range = bitmap & range_bitmap;
         for (auto pos : bitmap_in_range) {
            result[pos - range_start].remove(sequence_idx);
         }
      }
      return result;
   }

   template <typename SymbolType>
   void overwriteCoverageInSequence(std::vector<std::string>& sequences, const Bitmap& row_ids)
      const;

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
      archive & horizontal_bitmaps;
      archive & starts;
      archive & ends;
      archive & batch_min_start;
      archive & batch_max_start;
      archive & batch_min_end;
      archive & batch_max_end;
   }
};

/// The `coverageScan` implementations the index picks between at runtime. Production code calls
/// `coverageScan`, which dispatches on `cpuHasAvx512()`; these are declared so a test can run both
/// over the same input and assert they agree. Without that, the hand-written AVX-512 kernel is
/// only ever exercised on hardware that happens to support it, and never compared against
/// anything.
namespace detail {

/// Sets bit `row` of `bitset` for every row where `starts[row] <= position < ends[row]`, returning
/// the number of such rows. `bitset` must be a zeroed 1024-word (2^16-bit) buffer, and `starts`
/// and `ends` must each hold `num_rows` entries.
uint32_t coverageScanScalar(
   const uint32_t* starts,
   const uint32_t* ends,
   size_t num_rows,
   uint32_t position,
   uint64_t* bitset
);

/// Whether the running CPU has the AVX-512 subsets `coverageScanAvx512` needs. Always false where
/// this build has no x86 SIMD path at all.
bool cpuHasAvx512();

#ifdef RHYDB_HAS_X86_SIMD
/// Same contract as `coverageScanScalar`, 16 rows per iteration. Calling this requires
/// `cpuHasAvx512()`; it faults on a CPU without those subsets.
uint32_t coverageScanAvx512(
   const uint32_t* starts,
   const uint32_t* ends,
   size_t num_rows,
   uint32_t position,
   uint64_t* bitset
);
#endif

}  // namespace detail

}  // namespace rhydb::storage::column
