#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/row_compaction.h"
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

   /// Removes the rows `compaction` deletes and renumbers the remaining ones, see `RowCompaction`.
   void deleteRows(const RowCompaction& compaction);

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
   /// `[start, end)` and is not one of the row's in-region N positions).
   [[nodiscard]] roaring_util::RoaringContainer coveredRowsInChunk(
      uint32_t position,
      uint16_t chunk_id
   ) const;

   /// The rows of the whole index that cover `position`, assembled chunk by chunk from
   /// `coveredRowsInChunk`.
   [[nodiscard]] roaring::Roaring coveredRows(uint32_t position) const;

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

   template <typename SymbolType>
   void overwriteCoverageInSequence(
      std::vector<std::string>& sequences,
      const Bitmap& row_ids
   ) const;

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
