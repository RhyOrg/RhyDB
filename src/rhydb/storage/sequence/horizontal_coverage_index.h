#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "rhydb/common/buffer.h"
#include "rhydb/roaring_util/roaring_container.h"

// AVX-512 is x86-only; the project also targets Apple-Silicon macOS and wasm, where
// <immintrin.h>, the `target` attribute and `__builtin_cpu_supports` do not exist. The guard lives
// here rather than in the .cpp so that tests can see which implementations this build contains.
#if defined(__x86_64__) || defined(_M_X64)
#define RHYDB_HAS_X86_SIMD 1
#endif

namespace rhydb {
class Coverage;
}

namespace rhydb::storage::sequence {

/// A run of missing (N) positions `[begin, end)` inside a row's covered range.
struct MissingRange {
   uint32_t begin;
   uint32_t end;
};
static_assert(sizeof(MissingRange) == 8);

struct HorizontalCoverageIndexHeader {
   static constexpr uint32_t MAGIC = 0x49434852;  // "RHCI" in little-endian
   static constexpr uint32_t VERSION = 1;

   uint32_t magic = MAGIC;
   uint32_t version = VERSION;
   uint32_t num_rows = 0;
   uint32_t num_rows_with_missing = 0;
   uint64_t num_missing_ranges = 0;
   /// The envelope of the rows' covered ranges: the smallest / largest start and end. For a chunk
   /// without rows, the minima are UINT32_MAX and the maxima 0.
   uint32_t min_start = UINT32_MAX;
   uint32_t max_start = 0;
   uint32_t min_end = UINT32_MAX;
   uint32_t max_end = 0;
};
static_assert(sizeof(HorizontalCoverageIndexHeader) == 40);

/// The horizontal coverage index of a single 2^16-row ingestion chunk: for every row, the covered
/// `[start, end)` range of its sequence and the missing (N) positions within that range. A row
/// covers a position iff the position lies in its range and is not one of its missing positions.
///
/// It is stored as one immutable, position-independent binary blob. After the header follow these
/// sections, each starting at a 64-byte aligned offset, in this order:
///
///    starts                    uint32_t[num_rows]
///    ends                      uint32_t[num_rows]
///    rows_with_missing         uint32_t[num_rows_with_missing]      ascending row indexes
///    missing_range_offsets     uint32_t[num_rows_with_missing + 1]  into missing_ranges
///    missing_ranges            MissingRange[num_missing_ranges]     ascending, disjoint per row
///
/// Values are in native byte order. Since nothing in the blob is a pointer, loading an index is a
/// plain read of its bytes into an aligned `common::Buffer` (or, later, a memory mapping of them),
/// checked once with `fromBuffer`; after that, `viewTrusted` views it in O(1).
///
/// `HorizontalCoverageIndex` itself is a non-owning view of such a blob, like
/// `RoaringContainerView`: whoever creates it keeps the buffer alive for as long as the view is
/// used (`SequenceChunk` holds both).
class HorizontalCoverageIndex {
   HorizontalCoverageIndexHeader header;
   std::span<const uint32_t> starts_;
   std::span<const uint32_t> ends_;
   std::span<const uint32_t> rows_with_missing;
   std::span<const uint32_t> missing_range_offsets;
   std::span<const MissingRange> missing_ranges;

   /// Views the sections of `buffer`, whose size must match its header's section lengths.
   explicit HorizontalCoverageIndex(const common::Buffer& buffer);

   /// Whether one of the missing ranges of the `missing_idx`-th row with missing positions
   /// contains `position`.
   [[nodiscard]] bool missingAt(size_t missing_idx, uint32_t position) const;

  public:
   static constexpr size_t ALIGNMENT = common::Buffer::ALIGNMENT;

   /// The size of an index blob with the given section lengths.
   [[nodiscard]] static size_t blobSize(
      uint32_t num_rows,
      uint32_t num_rows_with_missing,
      size_t num_missing_ranges
   );

   /// Views `buffer` as an index. Checks the header, the section lengths, the envelope and that
   /// the missing ranges are sorted, disjoint and inside their row's covered range. Throws
   /// `std::runtime_error` if `buffer` does not hold a valid index.
   [[nodiscard]] static HorizontalCoverageIndex fromBuffer(const common::Buffer& buffer);

   /// A view must not outlive its buffer.
   static HorizontalCoverageIndex fromBuffer(const common::Buffer&& buffer) = delete;

   /// Views `buffer`, which holds a valid index (one this process built, or that `fromBuffer` has
   /// checked), without checking it again: O(1), it only reads the header.
   [[nodiscard]] static HorizontalCoverageIndex viewTrusted(const common::Buffer& buffer) {
      return HorizontalCoverageIndex{buffer};
   }

   /// A view must not outlive its buffer.
   static HorizontalCoverageIndex viewTrusted(const common::Buffer&& buffer) = delete;

   [[nodiscard]] uint32_t numRows() const { return header.num_rows; }

   [[nodiscard]] std::span<const uint32_t> starts() const { return starts_; }

   [[nodiscard]] std::span<const uint32_t> ends() const { return ends_; }

   [[nodiscard]] std::pair<uint32_t, uint32_t> coverageRange(uint16_t row) const {
      return {starts_[row], ends_[row]};
   }

   [[nodiscard]] uint32_t minStart() const { return header.min_start; }
   [[nodiscard]] uint32_t maxStart() const { return header.max_start; }
   [[nodiscard]] uint32_t minEnd() const { return header.min_end; }
   [[nodiscard]] uint32_t maxEnd() const { return header.max_end; }

   /// The missing ranges of `row`, in ascending order; empty if it has none.
   [[nodiscard]] std::span<const MissingRange> missingRanges(uint16_t row) const;

   /// Calls `callback(uint16_t row, std::span<const MissingRange>)` for every row that has missing
   /// positions inside its covered range, in ascending row order.
   template <typename Callback>
   void forEachRowWithMissing(Callback&& callback) const {
      for (size_t missing_idx = 0; missing_idx < rows_with_missing.size(); ++missing_idx) {
         const uint32_t begin = missing_range_offsets[missing_idx];
         const uint32_t end = missing_range_offsets[missing_idx + 1];
         callback(
            static_cast<uint16_t>(rows_with_missing[missing_idx]),
            missing_ranges.subspan(begin, end - begin)
         );
      }
   }

   [[nodiscard]] uint32_t numRowsWithMissing() const { return header.num_rows_with_missing; }

   /// Whether `row` covers `position`.
   [[nodiscard]] bool covers(uint16_t row, uint32_t position) const;

   /// The rows that cover `position`, as one roaring container of row-in-chunk indexes (empty if
   /// no row does). Skips the scan when the envelope shows that no row or every row covers it.
   [[nodiscard]] roaring_util::RoaringContainer coveredRows(uint32_t position) const;

   /// True if no row covers `position` -- it lies outside the envelope `[min_start, max_end)`.
   /// O(1); lets a caller skip building the (empty) covered set.
   [[nodiscard]] bool noRowCovers(uint32_t position) const;

   /// True if *every* row covers `position`: it lies in the intersection of all covered ranges,
   /// `[max_start, min_end)`, and no row has a missing position there. When this holds and no
   /// mutation is stored at the position, every row carries the reference symbol, so the caller
   /// can treat the whole chunk as one group without materializing the covered set.
   [[nodiscard]] bool everyRowCovers(uint32_t position) const;

   /// Bytes taken up by the missing positions (row list, offsets and ranges).
   [[nodiscard]] size_t missingSizeInBytes() const;
};

/// Writes a `HorizontalCoverageIndex`, one row at a time in row order.
class HorizontalCoverageIndexBuilder {
   std::vector<uint32_t> starts;
   std::vector<uint32_t> ends;
   std::vector<uint32_t> rows_with_missing;
   std::vector<uint32_t> missing_range_offsets{0};
   std::vector<MissingRange> missing_ranges;

  public:
   /// Adds a row covering `coverage`'s range except for its missing positions; missing positions
   /// outside the range are ignored.
   void addRow(const Coverage& coverage);

   /// Adds a row covering `[start, end)` except for `missing_positions`, which need not be sorted;
   /// those outside the range are ignored.
   void addRow(uint32_t start, uint32_t end, std::span<const uint32_t> missing_positions);

   /// Adds a row covering `[start, end)` except for `missing_ranges`, which must be ascending,
   /// disjoint, non-empty and inside the range (as `HorizontalCoverageIndex::missingRanges` returns
   /// them).
   void addRow(uint32_t start, uint32_t end, std::span<const MissingRange> missing_ranges);

   /// Adds a null row, which covers no position.
   void addNullRow() { addRow(0, 0, std::span<const uint32_t>{}); }

   [[nodiscard]] uint32_t numRows() const { return static_cast<uint32_t>(starts.size()); }

   /// The index's blob, to be viewed with `HorizontalCoverageIndex::fromBuffer`.
   [[nodiscard]] common::Buffer finish() &&;
};

/// The number of rows of all `chunks` covering each position of a genome of `genome_length`.
[[nodiscard]] std::vector<uint64_t> computeCoverageCardinalities(
   std::span<const HorizontalCoverageIndex* const> chunks,
   size_t genome_length
);

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

}  // namespace rhydb::storage::sequence
