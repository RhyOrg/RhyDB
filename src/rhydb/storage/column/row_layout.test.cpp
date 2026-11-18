#include "rhydb/storage/column/row_layout.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/row_id.h"

using rhydb::storage::column::COLUMN_CHUNK_SIZE;
using rhydb::storage::column::RowId;
using rhydb::storage::column::RowLayout;

TEST(RowLayout, ofBuildsLayoutFromVariadicChunkSizes) {
   const RowLayout layout = RowLayout::of(3, 5, 2);

   ASSERT_EQ(layout.numChunks(), 3);
   ASSERT_EQ(layout.chunkSize(0), 3);
   ASSERT_EQ(layout.chunkSize(1), 5);
   ASSERT_EQ(layout.chunkSize(2), 2);
   ASSERT_EQ(layout.numRows(), 10);
}

TEST(RowLayout, ofIsEquivalentToRepeatedAppendChunk) {
   RowLayout appended;
   appended.appendChunk(4);
   appended.appendChunk(6);
   appended.appendChunk(7);

   const RowLayout factory = RowLayout::of(4, 6, 7);

   ASSERT_EQ(factory.numChunks(), appended.numChunks());
   ASSERT_EQ(factory.numRows(), appended.numRows());
   for (size_t chunk_id = 0; chunk_id < factory.numChunks(); ++chunk_id) {
      ASSERT_EQ(factory.chunkSize(chunk_id), appended.chunkSize(chunk_id));
   }
}

TEST(RowLayout, ofWithNoArgumentsIsEmpty) {
   const RowLayout layout = RowLayout::of();

   ASSERT_EQ(layout.numChunks(), 0);
   ASSERT_EQ(layout.numRows(), 0);
   ASSERT_EQ(layout.begin(), layout.end());
}

TEST(RowLayout, defaultConstructedIsEmpty) {
   const RowLayout layout;

   ASSERT_EQ(layout.numChunks(), 0);
   ASSERT_EQ(layout.numRows(), 0);
}

TEST(RowLayout, appendChunkAccumulatesRows) {
   RowLayout layout;
   ASSERT_EQ(layout.numRows(), 0);
   layout.appendChunk(3);
   ASSERT_EQ(layout.numRows(), 3);
   layout.appendChunk(5);
   ASSERT_EQ(layout.numRows(), 8);
}

TEST(RowLayout, iteratorVisitsEveryRowChunkByChunk) {
   const RowLayout layout = RowLayout::of(2, 3);

   std::vector<RowId> visited;
   for (const RowId row_id : layout) {
      visited.push_back(row_id);
   }

   ASSERT_THAT(
      visited,
      ::testing::ElementsAre(
         RowId{.chunk_id = 0, .row_in_chunk = 0},
         RowId{.chunk_id = 0, .row_in_chunk = 1},
         RowId{.chunk_id = 1, .row_in_chunk = 0},
         RowId{.chunk_id = 1, .row_in_chunk = 1},
         RowId{.chunk_id = 1, .row_in_chunk = 2}
      )
   );
}

TEST(RowLayout, iteratorAdvancesPastFullChunkAtUint16Max) {
   // A chunk of exactly COLUMN_CHUNK_SIZE (2^16) rows is the only way the last row's `row_in_chunk`
   // reaches UINT16_MAX, which exercises the first clause of the if-branch in operator++. The full
   // chunk size must also round-trip through the uint16 `row_in_chunk` arithmetic so that advancing
   // past the last row rolls over to the next chunk instead of wrapping back to row 0.
   const RowLayout layout = RowLayout::of(COLUMN_CHUNK_SIZE, 1);

   ASSERT_EQ(layout.chunkSize(0), COLUMN_CHUNK_SIZE);
   ASSERT_EQ(layout.numRows(), static_cast<uint32_t>(COLUMN_CHUNK_SIZE) + 1);

   std::vector<RowId> visited;
   for (const RowId row_id : layout) {
      visited.push_back(row_id);
   }

   ASSERT_EQ(visited.size(), static_cast<size_t>(COLUMN_CHUNK_SIZE) + 1);
   ASSERT_EQ(visited.front(), (RowId{.chunk_id = 0, .row_in_chunk = 0}));
   // The last row of the full chunk sits at UINT16_MAX...
   ASSERT_EQ(visited[COLUMN_CHUNK_SIZE - 1], (RowId{.chunk_id = 0, .row_in_chunk = UINT16_MAX}));
   // ...and advancing past it rolls over into the next chunk rather than overflowing to row 0.
   ASSERT_EQ(visited.back(), (RowId{.chunk_id = 1, .row_in_chunk = 0}));
}

TEST(RowLayout, appendChunkRejectsEmptyChunk) {
   RowLayout layout;
   ASSERT_THROW(layout.appendChunk(0), std::runtime_error);
}

TEST(RowLayout, ofRejectsEmptyChunk) {
   ASSERT_THROW(RowLayout::of(2, 0, 3), std::runtime_error);
}
