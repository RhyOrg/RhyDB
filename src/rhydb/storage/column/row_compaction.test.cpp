#include "rhydb/storage/column/row_compaction.h"

#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/roaring_util/roaring_container.h"

using rhydb::roaring_util::RoaringContainer;
using rhydb::storage::column::RowCompaction;
using rhydb::storage::column::RowId;
using rhydb::storage::column::RowLayout;

namespace {
uint32_t globalId(uint16_t chunk_id, uint16_t row_in_chunk) {
   return RowId{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk}.toGlobal();
}

/// Splits `rows` into one container per chunk of a layout with `num_chunks` chunks.
std::vector<RoaringContainer> perChunk(const roaring::Roaring& rows, size_t num_chunks) {
   std::vector<RoaringContainer> result;
   result.reserve(num_chunks);
   for (size_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
      result.push_back(RoaringContainer::withCapacity(0));
   }
   for (const auto& [chunk_id, chunk_rows] : rhydb::Bitmap{&rows}) {
      result.at(chunk_id) = chunk_rows.toOwning();
   }
   return result;
}

/// Compacts `rows` as per-chunk containers and returns the result as global row ids.
roaring::Roaring compacted(
   const RowCompaction& compaction,
   const roaring::Roaring& rows,
   size_t num_chunks
) {
   auto containers = perChunk(rows, num_chunks);
   compaction.compact(containers);
   EXPECT_EQ(containers.size(), compaction.compactedLayout().numChunks());
   return rhydb::Bitmap::fromChunkContainers(containers).toRoaring();
}
}  // namespace

TEST(RowCompaction, packsTheRemainingRowsOfEachChunkAndDropsEmptyChunks) {
   // Chunk 1 loses all its rows, chunks 0 and 2 lose some
   const roaring::Roaring deleted{
      globalId(0, 1),
      globalId(1, 0),
      globalId(1, 1),
      globalId(2, 0),
      globalId(2, 2),
   };
   const RowCompaction compaction{RowLayout::of(4, 2, 3), rhydb::Bitmap{&deleted}};

   EXPECT_EQ(compaction.compactedLayout().numChunks(), 2);
   EXPECT_EQ(compaction.compactedLayout().chunkSize(0), 3);
   EXPECT_EQ(compaction.compactedLayout().chunkSize(1), 1);

   EXPECT_EQ(compaction.newChunkId(0), std::optional<uint16_t>{0});
   EXPECT_EQ(compaction.newChunkId(1), std::nullopt);
   EXPECT_EQ(compaction.newChunkId(2), std::optional<uint16_t>{1});

   EXPECT_EQ(compaction.map({.chunk_id = 0, .row_in_chunk = 0}), (RowId{0, 0}));
   EXPECT_EQ(compaction.map({.chunk_id = 0, .row_in_chunk = 1}), std::nullopt);
   EXPECT_EQ(compaction.map({.chunk_id = 0, .row_in_chunk = 3}), (RowId{0, 2}));
   EXPECT_EQ(compaction.map({.chunk_id = 1, .row_in_chunk = 1}), std::nullopt);
   EXPECT_EQ(compaction.map({.chunk_id = 2, .row_in_chunk = 1}), (RowId{1, 0}));

   const roaring::Roaring bitmap{
      globalId(0, 0),
      globalId(0, 1),
      globalId(0, 3),
      globalId(1, 1),
      globalId(2, 1),
   };
   EXPECT_EQ(
      compacted(compaction, bitmap, 3),
      (roaring::Roaring{globalId(0, 0), globalId(0, 2), globalId(1, 0)})
   );
   EXPECT_EQ(compacted(compaction, roaring::Roaring{globalId(1, 0)}, 3), roaring::Roaring{});

   std::vector<std::vector<char>> per_chunk{{'a', 'b', 'c', 'd'}, {'e', 'f'}, {'g', 'h', 'i'}};
   compaction.compactChunks(per_chunk);
   EXPECT_EQ(per_chunk, (std::vector<std::vector<char>>{{'a', 'c', 'd'}, {'h'}}));
}

TEST(RowCompaction, keepsChunksWithoutDeletedRowsAsTheyAre) {
   const RowCompaction compaction{RowLayout::of(3, 3), rhydb::Bitmap{}};

   EXPECT_EQ(compaction.compactedLayout().numRows(), 6);
   const roaring::Roaring original{globalId(0, 2), globalId(1, 0), globalId(1, 2)};
   EXPECT_EQ(compacted(compaction, original, 2), original);
}

TEST(RowCompaction, deletingAllRowsLeavesNoChunk) {
   RowLayout layout = RowLayout::of(2, 1);
   const RowCompaction compaction{layout, rhydb::Bitmap{layout.fullBitmap()}};

   EXPECT_EQ(compaction.compactedLayout().numChunks(), 0);
   EXPECT_EQ(compacted(compaction, layout.fullBitmap(), 2), roaring::Roaring{});
}

TEST(RowCompaction, compactsBitsetAndRunContainersIntoValidContainers) {
   // Deletes three of every four rows of a chunk of 40000 rows, leaving 10000
   roaring::Roaring deleted;
   for (uint32_t row = 0; row < 40000; ++row) {
      if (row % 4 != 0) {
         deleted.add(globalId(1, static_cast<uint16_t>(row)));
      }
   }
   const RowCompaction compaction{RowLayout::of(3, 40000), rhydb::Bitmap{&deleted}};

   auto all_rows = perChunk(RowLayout::of(3, 40000).fullBitmap(), 2);
   compaction.compact(all_rows);
   EXPECT_EQ(
      rhydb::Bitmap::fromChunkContainers(all_rows).toRoaring(),
      compaction.compactedLayout().fullBitmap()
   );
   EXPECT_EQ(all_rows[1].getTypecode(), RUN_CONTAINER_TYPE);

   // A bitset container of 15000 rows, of which 3000 isolated rows remain: too few for a bitset
   // container and too many runs for a run container, so they must end up in an array container
   roaring::Roaring sparse_rows;
   roaring::Roaring expected;
   for (uint32_t row = 0; row < 40000; ++row) {
      if (row < 24000 ? row % 8 == 0 : row % 4 != 0) {
         sparse_rows.add(globalId(1, static_cast<uint16_t>(row)));
      }
      if (row < 24000 && row % 8 == 0) {
         expected.add(globalId(1, static_cast<uint16_t>(row / 4)));
      }
   }
   auto sparse_containers = perChunk(sparse_rows, 2);
   ASSERT_EQ(sparse_containers[1].getTypecode(), BITSET_CONTAINER_TYPE);
   compaction.compact(sparse_containers);
   EXPECT_EQ(rhydb::Bitmap::fromChunkContainers(sparse_containers).toRoaring(), expected);
   EXPECT_EQ(sparse_containers[1].getTypecode(), ARRAY_CONTAINER_TYPE);
}
