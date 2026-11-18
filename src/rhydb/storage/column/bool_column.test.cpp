#include "rhydb/storage/column/bool_column.h"

#include <optional>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/storage/column/row_id.h"

using rhydb::storage::column::BoolColumn;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::RowCompaction;
using rhydb::storage::column::RowId;
using rhydb::storage::column::RowLayout;

namespace {
// Appends two chunks: {true, false, null} and {true, null, false}.
void appendTwoChunks(BoolColumn& column) {
   RHYDB_ASSERT(column.appendChunk({true, false, std::nullopt}).has_value());
   RHYDB_ASSERT(column.appendChunk({true, std::nullopt, false}).has_value());
}

roaring::Roaring globalRows(std::initializer_list<RowId> rows) {
   roaring::Roaring result;
   for (const auto row : rows) {
      result.add(row.toGlobal());
   }
   return result;
}
}  // namespace

TEST(BoolColumn, updateAssignsScalarValue) {
   ColumnMetadata column_metadata("bool_column");
   BoolColumn under_test{&column_metadata};
   appendTwoChunks(under_test);

   // A row of each kind becomes false, across both chunks.
   under_test.update(globalRows({RowId(0, 0), RowId(0, 2), RowId(1, 2)}), false);
   EXPECT_EQ(under_test.trueRows().toRoaring(), globalRows({RowId(1, 0)}));
   EXPECT_EQ(
      under_test.falseRows().toRoaring(),
      globalRows({RowId(0, 0), RowId(0, 1), RowId(0, 2), RowId(1, 2)})
   );
   EXPECT_EQ(under_test.nullRows().toRoaring(), globalRows({RowId(1, 1)}));

   under_test.update(globalRows({RowId(0, 1), RowId(1, 0)}), std::nullopt);
   EXPECT_EQ(under_test.trueRows().toRoaring(), roaring::Roaring{});
   EXPECT_EQ(
      under_test.falseRows().toRoaring(), globalRows({RowId(0, 0), RowId(0, 2), RowId(1, 2)})
   );
   EXPECT_EQ(
      under_test.nullRows().toRoaring(), globalRows({RowId(0, 1), RowId(1, 0), RowId(1, 1)})
   );
}

TEST(BoolColumn, updateChunkAssignsEachRowItsOwnValue) {
   ColumnMetadata column_metadata("bool_column");
   BoolColumn under_test{&column_metadata};
   appendTwoChunks(under_test);

   // In the second chunk: true -> null, null -> true, false -> true.
   const auto rows = globalRows({RowId(1, 0), RowId(1, 1), RowId(1, 2)});
   for (const auto& [chunk_id, chunk_rows] : rhydb::Bitmap{&rows}) {
      under_test.update(chunk_id, chunk_rows, {std::nullopt, true, true});
   }

   // Every row is in exactly one bitmap, and the first chunk is untouched.
   EXPECT_EQ(
      under_test.trueRows().toRoaring(), globalRows({RowId(0, 0), RowId(1, 1), RowId(1, 2)})
   );
   EXPECT_EQ(under_test.falseRows().toRoaring(), globalRows({RowId(0, 1)}));
   EXPECT_EQ(under_test.nullRows().toRoaring(), globalRows({RowId(0, 2), RowId(1, 0)}));
   EXPECT_EQ(under_test.chunkSize(1), 3);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(BoolColumn, deleteRowsCompactsEveryChunk) {
   ColumnMetadata column_metadata("bool_column");
   BoolColumn under_test{&column_metadata};
   appendTwoChunks(under_test);
   RHYDB_ASSERT(under_test.appendChunk({false, true}).has_value());

   // The first chunk loses its false row, the second chunk all its rows
   const RowCompaction compaction{
      RowLayout::of(3, 3, 2),
      rhydb::Bitmap{globalRows({RowId(0, 1), RowId(1, 0), RowId(1, 1), RowId(1, 2)})},
   };
   under_test.deleteRows(compaction);

   ASSERT_EQ(under_test.numChunks(), 2);
   EXPECT_EQ(under_test.chunkSize(0), 2);
   EXPECT_EQ(under_test.chunkSize(1), 2);
   EXPECT_EQ(under_test.trueRows().toRoaring(), globalRows({RowId(0, 0), RowId(1, 1)}));
   EXPECT_EQ(under_test.falseRows().toRoaring(), globalRows({RowId(1, 0)}));
   EXPECT_EQ(under_test.nullRows().toRoaring(), globalRows({RowId(0, 1)}));
}
