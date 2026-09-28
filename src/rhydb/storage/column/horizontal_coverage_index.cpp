#include "rhydb/storage/column/horizontal_coverage_index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <spdlog/spdlog.h>
#include <roaring/roaring.hh>

// AVX-512 is x86-only; the project also targets Apple-Silicon macOS and wasm, where <immintrin.h>,
// the `target` attribute and `__builtin_cpu_supports` do not exist. Everything AVX-512 lives behind
// this guard, with a scalar fallback used everywhere else.
// RHYDB_HAS_X86_SIMD is defined by horizontal_coverage_index.h, which the tests also see.
#ifdef RHYDB_HAS_X86_SIMD
#include <immintrin.h>
#endif

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"

namespace rhydb::storage::column {

void HorizontalCoverageIndex::insertCoverage(RowId row_id, const Coverage& coverage) {
   if (row_id.chunk_id == starts.size()) {
      starts.emplace_back();
      ends.emplace_back();
      batch_min_start.emplace_back(UINT32_MAX);
      batch_max_start.emplace_back(0);
      batch_min_end.emplace_back(UINT32_MAX);
      batch_max_end.emplace_back(0);
   }
   // For now, coverage needs to be inserted in ascending order
   RHYDB_ASSERT_EQ(starts.size(), ends.size());
   RHYDB_ASSERT(row_id.chunk_id == starts.size() - 1);
   RHYDB_ASSERT_EQ(row_id.row_in_chunk, starts.at(row_id.chunk_id).size());

   starts.at(row_id.chunk_id).push_back(coverage.start);
   ends.at(row_id.chunk_id).push_back(coverage.end);

   batch_min_start.back() = std::min(batch_min_start.back(), coverage.start);
   batch_max_start.back() = std::max(batch_max_start.back(), coverage.start);
   batch_min_end.back() = std::min(batch_min_end.back(), coverage.end);
   batch_max_end.back() = std::max(batch_max_end.back(), coverage.end);

   // We also have a row_wise bitmap, that covers all N symbols that are within the covered region
   roaring::Roaring horizontal_bitmap;
   horizontal_bitmap.addMany(coverage.missing_positions.size(), coverage.missing_positions.data());
   horizontal_bitmap.removeRange(0, coverage.start);
   horizontal_bitmap.removeRange(coverage.end, UINT32_MAX);
   horizontal_bitmap.runOptimize();
   horizontal_bitmap.shrinkToFit();

   if (horizontal_bitmap.cardinality() > 0) {
      horizontal_bitmaps.emplace(row_id.toGlobal(), std::move(horizontal_bitmap));
   }
}

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

}  // namespace

roaring_util::RoaringContainer HorizontalCoverageIndex::coveredRowsInChunk(
   uint32_t position,
   uint16_t chunk_id
) const {
   // No row in this chunk can cover the position.
   if (noRowCoversPositionInChunk(position, chunk_id)) {
      return roaring_util::RoaringContainer::withCapacity(1);
   }

   const auto& chunk_starts = starts[chunk_id];
   const auto num_rows = static_cast<uint32_t>(chunk_starts.size());

   // Fast path: if the position lies within the chunk's intersection envelope
   // `[batch_max_start, batch_min_end)`, every row in the chunk covers it.
   if (positionCoveredByWholeChunk(position, chunk_id)) {
      return {
         roaring::internal::run_container_create_range(0, num_rows), num_rows, RUN_CONTAINER_TYPE,
      };
   }

   auto* bitset = roaring::internal::bitset_container_create();
   bitset->cardinality = static_cast<int32_t>(
      coverageScan(chunk_starts.data(), ends[chunk_id].data(), num_rows, position, bitset->words)
   );

   // Remove this chunk's in-region N positions: a row whose covered range includes `position` but
   // records an N there is not covered at `position` (it belongs to the missing symbol instead).
   const uint32_t base_row_id = static_cast<uint32_t>(chunk_id) << 16U;
   const uint64_t chunk_end_key = static_cast<uint64_t>(base_row_id) + num_rows;
   const auto chunk_rows_begin = horizontal_bitmaps.lower_bound(base_row_id);
   const auto chunk_rows_end =
      chunk_end_key > UINT32_MAX
         ? horizontal_bitmaps.end()
         : horizontal_bitmaps.lower_bound(static_cast<uint32_t>(chunk_end_key));
   for (auto iter = chunk_rows_begin; iter != chunk_rows_end; ++iter) {
      if (iter->second.contains(position)) {
         roaring::internal::bitset_container_remove(bitset, static_cast<uint16_t>(iter->first));
      }
   }

   roaring_util::RoaringContainer result{
      bitset, static_cast<uint32_t>(bitset->cardinality), BITSET_CONTAINER_TYPE,
   };
   result.runOptimizeAndShrink();
   return result;
}

bool HorizontalCoverageIndex::noRowCoversPositionInChunk(uint32_t position, uint16_t chunk_id)
   const {
   if (chunk_id >= starts.size()) {
      return true;
   }
   return batch_max_end.at(chunk_id) <= position || batch_min_start.at(chunk_id) > position;
}

bool HorizontalCoverageIndex::positionCoveredByWholeChunk(uint32_t position, uint16_t chunk_id)
   const {
   if (chunk_id >= starts.size()) {
      return false;
   }
   // The envelope says every row's covered range includes `position` iff it lies in the
   // intersection of all those ranges, `[batch_max_start, batch_min_end)`. (A null row has range
   // [0, 0), forcing `batch_min_end` to 0, so this is always false for a chunk with nulls.)
   if (position < batch_max_start.at(chunk_id) || position >= batch_min_end.at(chunk_id)) {
      return false;
   }
   // Each row covers `position` only as the reference symbol if it records no in-region N there, so
   // reject the chunk if any of its in-region-N rows carries an N at `position`.
   const uint32_t base_row_id = static_cast<uint32_t>(chunk_id) << 16U;
   const uint64_t chunk_end_key = static_cast<uint64_t>(base_row_id) + starts[chunk_id].size();
   const auto chunk_rows_begin = horizontal_bitmaps.lower_bound(base_row_id);
   const auto chunk_rows_end =
      chunk_end_key > UINT32_MAX
         ? horizontal_bitmaps.end()
         : horizontal_bitmaps.lower_bound(static_cast<uint32_t>(chunk_end_key));
   for (auto iter = chunk_rows_begin; iter != chunk_rows_end; ++iter) {
      if (iter->second.contains(position)) {
         return false;
      }
   }
   return true;
}

void HorizontalCoverageIndex::insertNullSequence(RowId row_id) {
   insertCoverage(row_id, Coverage{.start = 0, .end = 0, .missing_positions = {}});
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::vector<uint64_t> HorizontalCoverageIndex::computeCoverageCardinalities(size_t genome_length
) const {
   std::vector<int64_t> coverage_changes(genome_length + 1, 0);
   for (size_t chunk_id = 0; chunk_id < starts.size(); ++chunk_id) {
      const auto& chunk_starts = starts[chunk_id];
      const auto& chunk_ends = ends[chunk_id];
      for (size_t row_in_chunk = 0; row_in_chunk < chunk_starts.size(); ++row_in_chunk) {
         const uint32_t start = chunk_starts[row_in_chunk];
         const uint32_t end = chunk_ends[row_in_chunk];
         RHYDB_ASSERT_LE(end, genome_length);
         coverage_changes[start] += 1;
         coverage_changes[end] -= 1;
      }
   }
   for (const auto& [_row_id, missing_positions] : horizontal_bitmaps) {
      for (const uint32_t position_idx : missing_positions) {
         RHYDB_ASSERT_LT(position_idx, genome_length);
         coverage_changes[position_idx] -= 1;
         coverage_changes[position_idx + 1] += 1;
      }
   }

   std::vector<uint64_t> cardinalities(genome_length);
   uint64_t cardinality = 0;
   for (size_t position_idx = 0; position_idx < genome_length; ++position_idx) {
      cardinality += coverage_changes[position_idx];
      RHYDB_ASSERT_GE(cardinality, 0UL);
      cardinalities[position_idx] = static_cast<uint32_t>(cardinality);
   }
   return cardinalities;
}

template <typename SymbolType>
void HorizontalCoverageIndex::overwriteCoverageInSequence(
   std::vector<std::string>& sequences,
   const Bitmap& row_ids
) const {
   uint32_t id_in_reconstructed_sequences = 0;
   for (const auto& [key, view] : row_ids) {
      const uint32_t base = static_cast<uint32_t>(key) << 16U;
      for (const uint16_t low_bits : view) {
         const uint32_t row_id = base | low_bits;
         const auto [start, end] = coverageRange(row_id);
         const size_t sequence_size = sequences.at(id_in_reconstructed_sequences).size();

         for (uint32_t position_idx = 0; position_idx < start; position_idx++) {
            sequences.at(id_in_reconstructed_sequences).at(position_idx) =
               SymbolType::symbolToChar(SymbolType::SYMBOL_MISSING);
         }
         for (uint32_t position_idx = end; position_idx < sequence_size; position_idx++) {
            sequences.at(id_in_reconstructed_sequences).at(position_idx) =
               SymbolType::symbolToChar(SymbolType::SYMBOL_MISSING);
         }

         auto iter = horizontal_bitmaps.find(row_id);
         if (iter != horizontal_bitmaps.end()) {
            const roaring::Roaring& n_bitmap = iter->second;
            for (const uint32_t position_idx : n_bitmap) {
               sequences.at(id_in_reconstructed_sequences).at(position_idx) =
                  SymbolType::symbolToChar(SymbolType::SYMBOL_MISSING);
            }
         }
         id_in_reconstructed_sequences++;
      }
   }
}

template void HorizontalCoverageIndex::overwriteCoverageInSequence<Nucleotide>(
   std::vector<std::string>& sequences,
   const Bitmap& row_ids
) const;
template void HorizontalCoverageIndex::overwriteCoverageInSequence<AminoAcid>(
   std::vector<std::string>& sequences,
   const Bitmap& row_ids
) const;

}  // namespace rhydb::storage::column
