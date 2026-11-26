#include "rhydb/storage/sequence/horizontal_coverage_index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/buffer.h"
#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::storage::sequence {

namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

std::vector<uint16_t> rowsIn(const roaring_util::RoaringContainer& container) {
   const roaring_util::RoaringContainerView view{container};
   return {view.begin(), roaring_util::RoaringContainerView::end()};
}

/// A copy of `bytes` in freshly allocated memory, as it would be after reading it back from disk.
common::Buffer copyOf(std::span<const std::byte> bytes) {
   common::BufferBuilder builder{bytes.size()};
   builder.write(0, bytes);
   return std::move(builder).finish();
}

common::Buffer exampleBuffer() {
   HorizontalCoverageIndexBuilder builder;
   // Unsorted, duplicated and partly out-of-range missing positions collapse into sorted ranges
   // inside [10, 40).
   const std::vector<uint32_t> missing{22, 20, 21, 5, 30, 21, 39, 45};
   builder.addRow(10, 40, std::span<const uint32_t>{missing});
   builder.addNullRow();
   builder.addRow(0, 50, std::span<const uint32_t>{});
   return std::move(builder).finish();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void expectExampleContent(const HorizontalCoverageIndex& index) {
   ASSERT_EQ(index.numRows(), 3);
   EXPECT_EQ(index.coverageRange(0), (std::pair<uint32_t, uint32_t>{10, 40}));
   EXPECT_EQ(index.coverageRange(1), (std::pair<uint32_t, uint32_t>{0, 0}));
   EXPECT_EQ(index.coverageRange(2), (std::pair<uint32_t, uint32_t>{0, 50}));

   const auto ranges = index.missingRanges(0);
   ASSERT_EQ(ranges.size(), 3);
   EXPECT_EQ(std::pair(ranges[0].begin, ranges[0].end), std::pair(20U, 23U));
   EXPECT_EQ(std::pair(ranges[1].begin, ranges[1].end), std::pair(30U, 31U));
   EXPECT_EQ(std::pair(ranges[2].begin, ranges[2].end), std::pair(39U, 40U));
   EXPECT_TRUE(index.missingRanges(1).empty());
   EXPECT_TRUE(index.missingRanges(2).empty());

   EXPECT_THAT(rowsIn(index.coveredRows(19)), ElementsAre(0, 2));
   EXPECT_THAT(rowsIn(index.coveredRows(22)), ElementsAre(2));
   EXPECT_TRUE(rowsIn(index.coveredRows(50)).empty());

   EXPECT_EQ(index.minStart(), 0);
   EXPECT_EQ(index.maxStart(), 10);
   EXPECT_EQ(index.minEnd(), 0);
   EXPECT_EQ(index.maxEnd(), 50);
}

/// A row as the builder is given it; a null row covers `[0, 0)`.
struct Row {
   uint32_t start = 0;
   uint32_t end = 0;
   std::vector<uint32_t> missing;

   [[nodiscard]] bool covers(uint32_t position) const {
      return start <= position && position < end &&
             std::ranges::find(missing, position) == missing.end();
   }
};

/// Random rows over a genome of `length`: null rows, empty and full ranges, and missing positions
/// in runs, unsorted, duplicated and partly outside the range. With `shared_core`, every row covers
/// the middle half of the genome except for its missing positions, so whole-chunk coverage occurs.
std::vector<Row> randomRows(
   std::mt19937& random,
   size_t num_rows,
   uint32_t length,
   bool shared_core
) {
   auto below = [&](uint32_t bound) { return static_cast<uint32_t>(random() % bound); };
   std::vector<Row> rows(num_rows);
   for (auto& row : rows) {
      if (shared_core) {
         row.start = below(length / 4);
         row.end = length - below(length / 4);
      } else if (below(10) != 0) {
         row.start = below(length + 1);
         row.end = below(4) == 0 ? row.start : row.start + below(length - row.start + 1);
      }
      if (below(shared_core ? 10 : 3) == 0) {
         for (uint32_t run = below(3) + 1; run > 0; --run) {
            const uint32_t begin = below(length + 10);
            for (uint32_t position = begin + below(5); position + 1 > begin; --position) {
               row.missing.push_back(position);
            }
         }
      }
   }
   return rows;
}

/// Adds `rows` through each of the builder's ways to add a row.
common::Buffer build(const std::vector<Row>& rows) {
   HorizontalCoverageIndexBuilder builder;
   for (size_t row = 0; row < rows.size(); ++row) {
      const auto& [start, end, missing] = rows[row];
      if (start == 0 && end == 0 && row % 2 == 0) {
         builder.addNullRow();
      } else if (row % 3 == 0) {
         builder.addRow(Coverage{.start = start, .end = end, .missing_positions = missing});
      } else {
         builder.addRow(start, end, std::span<const uint32_t>{missing});
      }
   }
   EXPECT_EQ(builder.numRows(), rows.size());
   return std::move(builder).finish();
}

/// The maximal runs of `row`'s missing positions inside its covered range.
std::vector<std::pair<uint32_t, uint32_t>> expectedMissingRanges(const Row& row) {
   std::vector<std::pair<uint32_t, uint32_t>> ranges;
   for (uint32_t position = row.start; position < row.end; ++position) {
      if (std::ranges::find(row.missing, position) == row.missing.end()) {
         continue;
      }
      if (!ranges.empty() && ranges.back().second == position) {
         ++ranges.back().second;
      } else {
         ranges.emplace_back(position, position + 1);
      }
   }
   return ranges;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void expectMatchesRows(
   const HorizontalCoverageIndex& index,
   const std::vector<Row>& rows,
   uint32_t length
) {
   ASSERT_EQ(index.numRows(), rows.size());
   HorizontalCoverageIndexHeader envelope;
   HorizontalCoverageIndexBuilder rebuilt;
   for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
      const auto row = static_cast<uint16_t>(row_idx);
      const auto& [start, end, missing] = rows[row];
      EXPECT_EQ(index.coverageRange(row), std::pair(start, end));
      std::vector<std::pair<uint32_t, uint32_t>> ranges;
      for (const auto& range : index.missingRanges(row)) {
         ranges.emplace_back(range.begin, range.end);
      }
      EXPECT_EQ(ranges, expectedMissingRanges(rows[row])) << "row " << row;
      rebuilt.addRow(start, end, index.missingRanges(row));
      envelope.min_start = std::min(envelope.min_start, start);
      envelope.max_start = std::max(envelope.max_start, start);
      envelope.min_end = std::min(envelope.min_end, end);
      envelope.max_end = std::max(envelope.max_end, end);
   }
   EXPECT_EQ(index.minStart(), envelope.min_start);
   EXPECT_EQ(index.maxStart(), envelope.max_start);
   EXPECT_EQ(index.minEnd(), envelope.min_end);
   EXPECT_EQ(index.maxEnd(), envelope.max_end);

   size_t num_rows_with_missing = 0;
   index.forEachRowWithMissing([&](uint16_t row, std::span<const MissingRange> ranges) {
      EXPECT_EQ(ranges.data(), index.missingRanges(row).data());
      ++num_rows_with_missing;
   });
   EXPECT_EQ(num_rows_with_missing, index.numRowsWithMissing());

   // Rows added with the ranges the index returns make the same blob.
   const auto rebuilt_buffer = std::move(rebuilt).finish();
   const auto rebuilt_index = HorizontalCoverageIndex::fromBuffer(rebuilt_buffer);
   EXPECT_EQ(rebuilt_index.missingSizeInBytes(), index.missingSizeInBytes());

   for (uint32_t position = 0; position <= length + 1; ++position) {
      std::vector<uint16_t> expected;
      for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
         const auto row = static_cast<uint16_t>(row_idx);
         const bool covers = rows[row].covers(position);
         ASSERT_EQ(index.covers(row, position), covers) << "row " << row << " at " << position;
         if (covers) {
            expected.push_back(row);
         }
      }
      ASSERT_EQ(rowsIn(index.coveredRows(position)), expected) << "at " << position;
      ASSERT_EQ(index.everyRowCovers(position), !rows.empty() && expected.size() == rows.size())
         << "at " << position;
      ASSERT_EQ(
         index.noRowCovers(position), position < envelope.min_start || position >= envelope.max_end
      ) << "at "
        << position;
   }
}

}  // namespace

TEST(HorizontalCoverageIndex, collapsesMissingPositionsIntoRangesAndRereadsInPlace) {
   const auto buffer = exampleBuffer();
   expectExampleContent(HorizontalCoverageIndex::fromBuffer(buffer));

   const auto copy = copyOf(buffer.bytes());
   const auto index = HorizontalCoverageIndex::fromBuffer(copy);
   expectExampleContent(index);
   const auto* starts = reinterpret_cast<const std::byte*>(index.starts().data());  // NOLINT
   EXPECT_GE(starts, copy.bytes().data());
   EXPECT_LT(starts, copy.bytes().data() + copy.size());
}

TEST(HorizontalCoverageIndex, emptyIndexHasNoRowsAndAnEmptyEnvelope) {
   const auto buffer = HorizontalCoverageIndexBuilder{}.finish();
   const auto index = HorizontalCoverageIndex::fromBuffer(buffer);
   EXPECT_EQ(index.numRows(), 0);
   EXPECT_TRUE(index.noRowCovers(0));
   EXPECT_FALSE(index.everyRowCovers(0));
   EXPECT_TRUE(rowsIn(index.coveredRows(0)).empty());
   const auto copy = copyOf(buffer.bytes());
   EXPECT_EQ(HorizontalCoverageIndex::fromBuffer(copy).numRows(), 0);
}

// Every query of the index, at every position, against the coverage of the rows it was built from:
// the envelope fast paths (no row / every row covers), the scan around its 16-row stride, and the
// in-region missing positions. Also checks the cardinalities summed over several chunks.
TEST(HorizontalCoverageIndex, matchesTheRowsItWasBuiltFrom) {
   std::mt19937 random{20260922};  // NOLINT(bugprone-random-generator-seed): reproducible
   constexpr uint32_t LENGTH = 120;
   std::vector<std::vector<Row>> chunk_rows;
   for (const size_t num_rows : {1, 17, 100, 300}) {
      chunk_rows.push_back(randomRows(random, num_rows, LENGTH, false));
      chunk_rows.push_back(randomRows(random, num_rows, LENGTH, true));
   }

   std::vector<common::Buffer> buffers;
   std::vector<HorizontalCoverageIndex> chunks;
   for (const auto& rows : chunk_rows) {
      buffers.push_back(copyOf(build(rows).bytes()));
      chunks.push_back(HorizontalCoverageIndex::fromBuffer(buffers.back()));
      SCOPED_TRACE(rows.size());
      expectMatchesRows(chunks.back(), rows, LENGTH);
   }

   std::vector<const HorizontalCoverageIndex*> pointers;
   std::vector<uint64_t> expected(LENGTH, 0);
   for (size_t chunk = 0; chunk < chunks.size(); ++chunk) {
      pointers.push_back(&chunks[chunk]);
      for (uint32_t position = 0; position < LENGTH; ++position) {
         expected[position] += std::ranges::count_if(chunk_rows[chunk], [&](const Row& row) {
            return row.covers(position);
         });
      }
   }
   EXPECT_EQ(computeCoverageCardinalities(pointers, LENGTH), expected);
}

TEST(HorizontalCoverageIndex, rejectsMalformedBytes) {
   const auto original_buffer = exampleBuffer();
   const std::vector<std::byte> original{
      original_buffer.bytes().begin(), original_buffer.bytes().end()
   };
   auto header_change = [](const std::function<void(HorizontalCoverageIndexHeader&)>& change) {
      return [change](std::vector<std::byte>& bytes) {
         HorizontalCoverageIndexHeader header;
         std::memcpy(&header, bytes.data(), sizeof(header));
         change(header);
         std::memcpy(bytes.data(), &header, sizeof(header));
      };
   };
   const std::vector<std::pair<std::function<void(std::vector<std::byte>&)>, std::string>> cases{
      {header_change([](auto& header) { header.magic = 0; }), "wrong magic number"},
      {header_change([](auto& header) { header.num_rows = 100; }), "do not match"},
      {header_change([](auto& header) { header.max_end = 49; }), "envelope does not match"},
      {[](auto& bytes) { bytes.resize(bytes.size() - 8); }, "do not match"},
      {[](auto& bytes) { bytes.resize(10); }, "cannot hold a header"},
      // Row 0's first missing range starts before its covered range.
      {
         [](auto& bytes) {
            const MissingRange bad{.begin = 5, .end = 6};
            std::memcpy(
               bytes.data() + bytes.size() - (3 * sizeof(MissingRange)), &bad, sizeof(bad)
            );
         },
         "not sorted, disjoint and covered",
      },
   };
   for (const auto& [corrupt, message] : cases) {
      auto bytes = original;
      corrupt(bytes);
      EXPECT_THAT(
         [&]() {
            const auto copy = copyOf(bytes);
            static_cast<void>(HorizontalCoverageIndex::fromBuffer(copy));
         },
         ThrowsMessage<std::runtime_error>(HasSubstr(message))
      );
   }
}

// coverageScan: the scalar and AVX-512 kernels against the specification
namespace {

constexpr size_t BITSET_WORDS = 1024;  // 2^16 bits, one per row of a chunk
constexpr uint32_t SCAN_LENGTH = 512;

/// Random `(starts, ends)` for row counts around the AVX-512 kernel's 16-row stride and the
/// bitset's 64-row word, so the tail loop and each in-word bit offset (0/16/32/48) are hit. Ranges
/// may be empty, inverted, touch 0 or the end of the genome, or span all of it.
std::vector<std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> scanInputs() {
   std::mt19937 random{20260922};  // NOLINT(bugprone-random-generator-seed): reproducible
   std::vector<std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> inputs;
   for (const size_t num_rows :
        {0, 1, 15, 16, 17, 31, 32, 33, 48, 63, 64, 65, 79, 128, 1000, 4096}) {
      auto& [starts, ends] = inputs.emplace_back();
      for (size_t row = 0; row < num_rows; ++row) {
         uint32_t start = random() % (SCAN_LENGTH + 1);
         uint32_t end = random() % (SCAN_LENGTH + 1);
         // 0: full, 1: empty, 2: possibly inverted, 3 / 4: touching an end, else ordinary
         const auto shape = random() % 10;
         if (shape > 2) {
            std::tie(start, end) = std::minmax(start, end);
         }
         if (shape == 0 || shape == 3) {
            start = 0;
         }
         if (shape == 0 || shape == 4) {
            end = SCAN_LENGTH;
         }
         if (shape == 1) {
            end = start;
         }
         starts.push_back(start);
         ends.push_back(end);
      }
   }
   return inputs;
}

using ScanKernel = uint32_t (*)(const uint32_t*, const uint32_t*, size_t, uint32_t, uint64_t*);

/// Runs `kernel` at every position and compares it with the specification: bit `row` (word
/// `row / 64`, bit `row % 64`) iff `starts[row] <= position < ends[row]`.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void expectKernelMatchesSpecification(ScanKernel kernel) {
   for (const auto& [starts, ends] : scanInputs()) {
      for (uint32_t position = 0; position <= SCAN_LENGTH; ++position) {
         std::vector<uint64_t> expected(BITSET_WORDS, 0);
         uint32_t expected_cardinality = 0;
         for (size_t row = 0; row < starts.size(); ++row) {
            if (starts[row] <= position && position < ends[row]) {
               expected[row / 64] |= uint64_t{1} << (row % 64);
               ++expected_cardinality;
            }
         }
         std::vector<uint64_t> bitset(BITSET_WORDS, 0);
         const uint32_t cardinality =
            kernel(starts.data(), ends.data(), starts.size(), position, bitset.data());
         ASSERT_EQ(cardinality, expected_cardinality) << starts.size() << " rows at " << position;
         ASSERT_EQ(bitset, expected) << starts.size() << " rows at " << position;
      }
   }
}

}  // namespace

TEST(HorizontalCoverageIndexScan, scalarMatchesSpecification) {
   expectKernelMatchesSpecification(detail::coverageScanScalar);
}

// The AVX-512 kernel can only run where the CPU supports it; elsewhere (e.g. Apple Silicon) this
// reports as skipped. It is the only check of that kernel, so make sure it runs somewhere.
TEST(HorizontalCoverageIndexScan, avx512MatchesSpecification) {
#ifndef RHYDB_HAS_X86_SIMD
   GTEST_SKIP() << "this build has no x86 SIMD path, so there is no AVX-512 kernel to compare";
#else
   if (!detail::cpuHasAvx512()) {
      GTEST_SKIP() << "CPU lacks avx512f/avx512bw, so coverageScanAvx512 cannot be executed here";
   }
   expectKernelMatchesSpecification(detail::coverageScanAvx512);
#endif
}

}  // namespace rhydb::storage::sequence
