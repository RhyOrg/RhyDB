#include "rhydb/storage/sequence/horizontal_coverage_index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <fmt/format.h>
#include <roaring/roaring.hh>

// AVX-512 is x86-only; the project also targets Apple-Silicon macOS and wasm, where <immintrin.h>,
// the `target` attribute and `__builtin_cpu_supports` do not exist. Everything AVX-512 lives behind
// this guard, with a scalar fallback used everywhere else.
// RHYDB_HAS_X86_SIMD is defined by horizontal_coverage_index.h, which the tests also see.
#ifdef RHYDB_HAS_X86_SIMD
#include <immintrin.h>
#endif

#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/panic.h"

namespace rhydb::storage::sequence {

namespace detail {

// non-simd implementation of "coverageScan"
uint32_t coverageScanScalar(
   const uint32_t* starts,
   const uint32_t* ends,
   size_t num_rows,
   uint32_t position,
   uint64_t* bitset
) {
   uint32_t cardinality = 0;
   for (size_t row = 0; row < num_rows; ++row) {
      if (starts[row] <= position && position < ends[row]) {
         bitset[row >> 6U] |= (uint64_t{1} << (row & 63U));
         ++cardinality;
      }
   }
   return cardinality;
}

#ifdef RHYDB_HAS_X86_SIMD
/// AVX-512 version: 16 rows per iteration. Each 16-bit compare mask lands inside one 64-bit
/// bitset word (row indices step by 16, so the bit offset is one of 0/16/32/48)
__attribute__((target("avx512f,avx512bw"))) uint32_t coverageScanAvx512(
   const uint32_t* starts,
   const uint32_t* ends,
   size_t num_rows,
   uint32_t position,
   uint64_t* bitset
) {
   const __m512i position_vec = _mm512_set1_epi32(static_cast<int>(position));
   uint32_t cardinality = 0;
   size_t row = 0;
   for (; row + 16 <= num_rows; row += 16) {
      const __m512i starts_vec = _mm512_loadu_si512(static_cast<const void*>(starts + row));
      const __m512i ends_vec = _mm512_loadu_si512(static_cast<const void*>(ends + row));
      const __mmask16 start_le = _mm512_cmp_epu32_mask(starts_vec, position_vec, _MM_CMPINT_LE);
      const __mmask16 end_gt = _mm512_cmp_epu32_mask(ends_vec, position_vec, _MM_CMPINT_NLE);
      const __mmask16 covered = start_le & end_gt;
      bitset[row >> 6U] |= (static_cast<uint64_t>(covered) << (row & 63U));
      cardinality += static_cast<uint32_t>(__builtin_popcount(covered));
   }
   for (; row < num_rows; ++row) {
      if (starts[row] <= position && position < ends[row]) {
         bitset[row >> 6U] |= (uint64_t{1} << (row & 63U));
         ++cardinality;
      }
   }
   return cardinality;
}

#endif

bool cpuHasAvx512() {
#ifdef RHYDB_HAS_X86_SIMD
   static const bool supported =
      __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
   return supported;
#else
   return false;
#endif
}

}  // namespace detail

namespace {

constexpr size_t alignUp(size_t value) {
   return (value + HorizontalCoverageIndex::ALIGNMENT - 1) / HorizontalCoverageIndex::ALIGNMENT *
          HorizontalCoverageIndex::ALIGNMENT;
}

/// The offsets of the sections of an index blob, and its total size.
struct Layout {
   size_t starts;
   size_t ends;
   size_t rows_with_missing;
   size_t missing_range_offsets;
   size_t missing_ranges;
   size_t size;

   Layout(uint32_t num_rows, uint32_t num_rows_with_missing, uint64_t num_missing_ranges)
       : starts(alignUp(sizeof(HorizontalCoverageIndexHeader))),
         ends(alignUp(starts + (num_rows * sizeof(uint32_t)))),
         rows_with_missing(alignUp(ends + (num_rows * sizeof(uint32_t)))),
         missing_range_offsets(
            alignUp(rows_with_missing + (num_rows_with_missing * sizeof(uint32_t)))
         ),
         missing_ranges(
            alignUp(missing_range_offsets + ((num_rows_with_missing + 1UL) * sizeof(uint32_t)))
         ),
         size(missing_ranges + (num_missing_ranges * sizeof(MissingRange))) {}
};

/// This method can compute a `bitset` that shows which rows in the index could cover `position`.
/// Sets bit `row` of `bitset` for every row where `starts[row] <= position < ends[row]`, returning
/// the number of such rows. `bitset` must be a zeroed 2^16-bit (1024-word) buffer.
uint32_t coverageScan(
   const uint32_t* starts,
   const uint32_t* ends,
   size_t num_rows,
   uint32_t position,
   uint64_t* bitset
) {
#ifdef RHYDB_HAS_X86_SIMD
   if (detail::cpuHasAvx512()) {
      return detail::coverageScanAvx512(starts, ends, num_rows, position, bitset);
   }
#endif
   return detail::coverageScanScalar(starts, ends, num_rows, position, bitset);
}

bool rangesContain(std::span<const MissingRange> ranges, uint32_t position) {
   // The first range ending after `position` is the only one that can contain it.
   const auto range = std::ranges::upper_bound(ranges, position, {}, &MissingRange::end);
   return range != ranges.end() && range->begin <= position;
}

}  // namespace

size_t HorizontalCoverageIndex::blobSize(
   uint32_t num_rows,
   uint32_t num_rows_with_missing,
   uint64_t num_missing_ranges
) {
   return Layout{num_rows, num_rows_with_missing, num_missing_ranges}.size;
}

HorizontalCoverageIndex::HorizontalCoverageIndex(const common::Buffer& buffer)
    : header(buffer.view<HorizontalCoverageIndexHeader>(0, 1)[0]) {
   const Layout layout{header.num_rows, header.num_rows_with_missing, header.num_missing_ranges};
   starts_ = buffer.view<uint32_t>(layout.starts, header.num_rows);
   ends_ = buffer.view<uint32_t>(layout.ends, header.num_rows);
   rows_with_missing =
      buffer.view<uint32_t>(layout.rows_with_missing, header.num_rows_with_missing);
   missing_range_offsets =
      buffer.view<uint32_t>(layout.missing_range_offsets, header.num_rows_with_missing + 1UL);
   missing_ranges = buffer.view<MissingRange>(layout.missing_ranges, header.num_missing_ranges);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
HorizontalCoverageIndex HorizontalCoverageIndex::fromBuffer(const common::Buffer& buffer) {
   auto fail = [](std::string_view reason) {
      throw std::runtime_error(fmt::format("invalid horizontal coverage index: {}", reason));
   };
   if (buffer.size() < sizeof(HorizontalCoverageIndexHeader)) {
      fail(fmt::format("{} bytes cannot hold a header", buffer.size()));
   }
   const auto header = buffer.view<HorizontalCoverageIndexHeader>(0, 1)[0];
   if (header.magic != HorizontalCoverageIndexHeader::MAGIC) {
      fail("wrong magic number");
   }
   if (header.version != HorizontalCoverageIndexHeader::VERSION) {
      fail(
         fmt::format(
            "version {}, expected {}", header.version, HorizontalCoverageIndexHeader::VERSION
         )
      );
   }
   if (
      header.num_rows > (1U << 16) || header.num_rows_with_missing > header.num_rows ||
      header.num_missing_ranges > buffer.size() ||
      blobSize(header.num_rows, header.num_rows_with_missing, header.num_missing_ranges) !=
         buffer.size()
   ) {
      fail(
         fmt::format(
            "{} rows, {} of them with {} missing ranges, do not match {} bytes",
            header.num_rows,
            header.num_rows_with_missing,
            header.num_missing_ranges,
            buffer.size()
         )
      );
   }

   HorizontalCoverageIndex index{buffer};

   HorizontalCoverageIndexHeader envelope;
   for (uint32_t row = 0; row < header.num_rows; ++row) {
      const uint32_t start = index.starts_[row];
      const uint32_t end = index.ends_[row];
      if (start > end) {
         fail(fmt::format("row {} covers [{}, {})", row, start, end));
      }
      envelope.min_start = std::min(envelope.min_start, start);
      envelope.max_start = std::max(envelope.max_start, start);
      envelope.min_end = std::min(envelope.min_end, end);
      envelope.max_end = std::max(envelope.max_end, end);
   }
   if (
      envelope.min_start != header.min_start || envelope.max_start != header.max_start ||
      envelope.min_end != header.min_end || envelope.max_end != header.max_end
   ) {
      fail("the envelope does not match the rows' covered ranges");
   }

   if (
      index.missing_range_offsets[0] != 0 ||
      index.missing_range_offsets[header.num_rows_with_missing] != header.num_missing_ranges
   ) {
      fail("the missing range offsets do not span the missing ranges");
   }
   for (uint32_t missing_idx = 0; missing_idx < header.num_rows_with_missing; ++missing_idx) {
      const uint32_t row = index.rows_with_missing[missing_idx];
      if (
         row >= header.num_rows ||
         (missing_idx > 0 && row <= index.rows_with_missing[missing_idx - 1])
      ) {
         fail(fmt::format("rows with missing positions are not ascending at {}", missing_idx));
      }
      const uint32_t begin = index.missing_range_offsets[missing_idx];
      const uint32_t end = index.missing_range_offsets[missing_idx + 1];
      if (end <= begin || end > header.num_missing_ranges) {
         fail(fmt::format("row {} has no or out-of-bounds missing ranges", row));
      }
      uint32_t previous_end = index.starts_[row];
      for (uint32_t range_idx = begin; range_idx < end; ++range_idx) {
         const MissingRange range = index.missing_ranges[range_idx];
         if (
            range.begin < previous_end || range.begin >= range.end || range.end > index.ends_[row]
         ) {
            fail(
               fmt::format(
                  "row {} has a missing range [{}, {}) that is not sorted, disjoint and covered",
                  row,
                  range.begin,
                  range.end
               )
            );
         }
         previous_end = range.end;
      }
   }
   return index;
}

std::span<const MissingRange> HorizontalCoverageIndex::missingRanges(uint16_t row) const {
   const auto found = std::ranges::lower_bound(rows_with_missing, uint32_t{row});
   if (found == rows_with_missing.end() || *found != row) {
      return {};
   }
   const auto missing_idx = static_cast<size_t>(found - rows_with_missing.begin());
   const uint32_t begin = missing_range_offsets[missing_idx];
   const uint32_t end = missing_range_offsets[missing_idx + 1];
   return missing_ranges.subspan(begin, end - begin);
}

bool HorizontalCoverageIndex::missingAt(size_t missing_idx, uint32_t position) const {
   const uint32_t begin = missing_range_offsets[missing_idx];
   const uint32_t end = missing_range_offsets[missing_idx + 1];
   return rangesContain(missing_ranges.subspan(begin, end - begin), position);
}

bool HorizontalCoverageIndex::covers(uint16_t row, uint32_t position) const {
   return starts_[row] <= position && position < ends_[row] &&
          !rangesContain(missingRanges(row), position);
}

roaring_util::RoaringContainer HorizontalCoverageIndex::coveredRows(uint32_t position) const {
   // No row in this chunk can cover the position.
   if (noRowCovers(position)) {
      return roaring_util::RoaringContainer::withCapacity(1);
   }

   // Fast path: if the position lies within the chunk's intersection envelope
   // `[max_start, min_end)` and no row misses it, every row in the chunk covers it.
   if (everyRowCovers(position)) {
      return roaring_util::RoaringContainer::ofRange(0, header.num_rows);
   }

   auto result = roaring_util::RoaringContainer::bitsetFilledBy([&](std::span<uint64_t> words) {
      coverageScan(starts_.data(), ends_.data(), header.num_rows, position, words.data());

      // Remove this chunk's in-region N positions: a row whose covered range includes `position`
      // but records an N there is not covered at `position` (it belongs to the missing symbol
      // instead).
      for (size_t missing_idx = 0; missing_idx < rows_with_missing.size(); ++missing_idx) {
         if (missingAt(missing_idx, position)) {
            const auto row = static_cast<uint16_t>(rows_with_missing[missing_idx]);
            words[row / 64U] &= ~(uint64_t{1} << (row % 64U));
         }
      }
   });
   result.runOptimizeAndShrink();
   return result;
}

bool HorizontalCoverageIndex::noRowCovers(uint32_t position) const {
   return header.max_end <= position || header.min_start > position;
}

bool HorizontalCoverageIndex::everyRowCovers(uint32_t position) const {
   // The envelope says every row's covered range includes `position` iff it lies in the
   // intersection of all those ranges, `[max_start, min_end)`. (A null row has range [0, 0),
   // forcing `min_end` to 0, so this is always false for a chunk with nulls.)
   if (header.num_rows == 0 || position < header.max_start || position >= header.min_end) {
      return false;
   }
   // Each row covers `position` only if it records no in-region N there.
   for (size_t missing_idx = 0; missing_idx < rows_with_missing.size(); ++missing_idx) {
      if (missingAt(missing_idx, position)) {
         return false;
      }
   }
   return true;
}

size_t HorizontalCoverageIndex::missingSizeInBytes() const {
   return rows_with_missing.size_bytes() + missing_range_offsets.size_bytes() +
          missing_ranges.size_bytes();
}

void HorizontalCoverageIndexBuilder::addRow(const Coverage& coverage) {
   addRow(coverage.start, coverage.end, coverage.missing_positions);
}

void HorizontalCoverageIndexBuilder::addRow(
   uint32_t start,
   uint32_t end,
   std::span<const uint32_t> missing_positions
) {
   RHYDB_ASSERT_LE(start, end);
   RHYDB_ASSERT_LT(starts.size(), 1UL << 16);
   const auto row = static_cast<uint32_t>(starts.size());
   starts.push_back(start);
   ends.push_back(end);

   // Only the missing positions inside the covered range matter; collapse them into ranges.
   std::vector<uint32_t> in_range;
   for (const uint32_t position : missing_positions) {
      if (start <= position && position < end) {
         in_range.push_back(position);
      }
   }
   if (in_range.empty()) {
      return;
   }
   std::ranges::sort(in_range);
   rows_with_missing.push_back(row);
   for (const uint32_t position : in_range) {
      if (
         missing_ranges.size() > missing_range_offsets.back() &&
         missing_ranges.back().end >= position
      ) {
         missing_ranges.back().end = std::max(missing_ranges.back().end, position + 1);
      } else {
         missing_ranges.push_back(MissingRange{.begin = position, .end = position + 1});
      }
   }
   missing_range_offsets.push_back(static_cast<uint32_t>(missing_ranges.size()));
}

void HorizontalCoverageIndexBuilder::addRow(
   uint32_t start,
   uint32_t end,
   std::span<const MissingRange> missing_ranges_of_row
) {
   RHYDB_ASSERT_LE(start, end);
   RHYDB_ASSERT_LT(starts.size(), 1UL << 16);
   const auto row = static_cast<uint32_t>(starts.size());
   starts.push_back(start);
   ends.push_back(end);
   if (missing_ranges_of_row.empty()) {
      return;
   }
   rows_with_missing.push_back(row);
   for (const auto& range : missing_ranges_of_row) {
      RHYDB_ASSERT(start <= range.begin && range.begin < range.end && range.end <= end);
      RHYDB_ASSERT(
         missing_ranges.size() == missing_range_offsets.back() ||
         missing_ranges.back().end < range.begin
      );
      missing_ranges.push_back(range);
   }
   missing_range_offsets.push_back(static_cast<uint32_t>(missing_ranges.size()));
}

common::Buffer HorizontalCoverageIndexBuilder::finish() && {
   HorizontalCoverageIndexHeader header;
   header.num_rows = static_cast<uint32_t>(starts.size());
   header.num_rows_with_missing = static_cast<uint32_t>(rows_with_missing.size());
   header.num_missing_ranges = missing_ranges.size();
   for (size_t row = 0; row < starts.size(); ++row) {
      header.min_start = std::min(header.min_start, starts[row]);
      header.max_start = std::max(header.max_start, starts[row]);
      header.min_end = std::min(header.min_end, ends[row]);
      header.max_end = std::max(header.max_end, ends[row]);
   }

   const Layout layout{header.num_rows, header.num_rows_with_missing, header.num_missing_ranges};
   common::BufferBuilder buffer{layout.size};
   buffer.write(0, std::span{&header, 1});
   buffer.write(layout.starts, std::span{starts});
   buffer.write(layout.ends, std::span{ends});
   buffer.write(layout.rows_with_missing, std::span{rows_with_missing});
   buffer.write(layout.missing_range_offsets, std::span{missing_range_offsets});
   buffer.write(layout.missing_ranges, std::span{missing_ranges});
   return std::move(buffer).finish();
}

std::vector<uint64_t> computeCoverageCardinalities(
   std::span<const HorizontalCoverageIndex* const> chunks,
   size_t genome_length
) {
   std::vector<int64_t> coverage_changes(genome_length + 1, 0);
   for (const auto* chunk_pointer : chunks) {
      const HorizontalCoverageIndex& chunk = *chunk_pointer;
      for (uint32_t row = 0; row < chunk.numRows(); ++row) {
         const auto [start, end] = chunk.coverageRange(static_cast<uint16_t>(row));
         RHYDB_ASSERT_LE(end, genome_length);
         coverage_changes[start] += 1;
         coverage_changes[end] -= 1;
      }
      chunk.forEachRowWithMissing([&](uint16_t /*row*/, std::span<const MissingRange> ranges) {
         for (const auto& [begin, end] : ranges) {
            coverage_changes[begin] -= 1;
            coverage_changes[end] += 1;
         }
      });
   }

   std::vector<uint64_t> cardinalities(genome_length);
   int64_t cardinality = 0;
   for (size_t position_idx = 0; position_idx < genome_length; ++position_idx) {
      cardinality += coverage_changes[position_idx];
      RHYDB_ASSERT_GE(cardinality, 0L);
      cardinalities[position_idx] = static_cast<uint64_t>(cardinality);
   }
   return cardinalities;
}

}  // namespace rhydb::storage::sequence
