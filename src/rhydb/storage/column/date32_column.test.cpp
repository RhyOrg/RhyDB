#include "rhydb/storage/column/date32_column.h"

#include <gtest/gtest.h>

#include "rhydb/common/date32.h"
#include "rhydb/storage/column/row_id.h"

using rhydb::storage::column::RowId;

TEST(Date32Column, insertValues) {
   rhydb::storage::column::ColumnMetadata column_metadata{"test_column"};
   rhydb::storage::column::Date32Column under_test(&column_metadata);

   std::vector<std::string> values_to_add{
      "2020-01-01", "2023-01-05", "2021-12-03", "2025-01-01", "2021-03-21"
   };
   rhydb::storage::column::Date32Column::Builder builder;
   for (const auto& value : values_to_add) {
      auto date = rhydb::common::stringToDate32(value);
      ASSERT_TRUE(date.has_value()) << date.error();
      builder.insert(date.value());
   }
   RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());

   ASSERT_EQ(under_test.numChunks(), 1);
   ASSERT_EQ(under_test.chunkSize(0), 5);

   for (size_t value_idx = 0; value_idx < values_to_add.size(); ++value_idx) {
      auto value = rhydb::common::date32ToString(under_test.getValue(RowId(0, value_idx)));
      ASSERT_EQ(value, values_to_add.at(value_idx));
   }
}

TEST(Date32Column, insertNull) {
   rhydb::storage::column::ColumnMetadata column_metadata{"test_column"};
   rhydb::storage::column::Date32Column under_test(&column_metadata);

   rhydb::storage::column::Date32Column::Builder builder;
   builder.insertNull();
   RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());

   ASSERT_EQ(under_test.numChunks(), 1);
   ASSERT_EQ(under_test.chunkSize(0), 1);
   ASSERT_TRUE(under_test.isNull(RowId(0, 0)));
}

TEST(Date32Column, parseInvalidDateReturnsError) {
   // Date parsing happens during phase-1 extraction (see stringToDate32), before
   // values reach the column builder.
   auto result = rhydb::common::stringToDate32("not-a-date");
   ASSERT_FALSE(result.has_value());
}

namespace {
rhydb::common::Date32 date(const std::string& value) {
   const auto parsed = rhydb::common::stringToDate32(value);
   RHYDB_ASSERT(parsed.has_value());
   return parsed.value();
}

rhydb::storage::column::Date32Column::Buffer chunkOf(const std::vector<std::string>& values) {
   rhydb::storage::column::Date32Column::Builder builder;
   for (const auto& value : values) {
      if (value.empty()) {
         builder.insertNull();
      } else {
         builder.insert(date(value));
      }
   }
   return builder.finalize();
}
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(Date32Column, staysSortedWhenChunkBoundaryIsGloballySorted) {
   rhydb::storage::column::ColumnMetadata column_metadata{"test_column"};
   rhydb::storage::column::Date32Column under_test(&column_metadata);

   // Each chunk is sorted on its own, and the first value of every later chunk is >= the last
   // value of the previous chunk, so the column is globally sorted across boundaries.
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2020-01-01", "2020-06-01"})).has_value());
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2020-06-01", "2021-03-21"})).has_value());
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2022-01-01", "2025-12-31"})).has_value());

   ASSERT_EQ(under_test.numChunks(), 3);
   ASSERT_EQ(under_test.chunkSize(0), 2);
   ASSERT_EQ(under_test.chunkSize(1), 2);
   ASSERT_EQ(under_test.chunkSize(2), 2);
   ASSERT_TRUE(under_test.isSorted());
}

TEST(Date32Column, notSortedWhenChunkBoundaryRegresses) {
   rhydb::storage::column::ColumnMetadata column_metadata{"test_column"};
   rhydb::storage::column::Date32Column under_test(&column_metadata);

   // Each chunk is sorted in isolation, but the second chunk starts before the first chunk ends.
   // Detecting this requires the cross-chunk ingestion state (last_appended_value); within-chunk
   // checks alone would wrongly report the column as sorted.
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2020-01-01", "2023-01-05"})).has_value());
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2021-12-03", "2025-01-01"})).has_value());

   ASSERT_EQ(under_test.numChunks(), 2);
   ASSERT_EQ(under_test.chunkSize(0), 2);
   ASSERT_EQ(under_test.chunkSize(1), 2);
   ASSERT_FALSE(under_test.isSorted());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(Date32Column, nullBitmapRowIdsAreOffsetAcrossChunks) {
   rhydb::storage::column::ColumnMetadata column_metadata{"test_column"};
   rhydb::storage::column::Date32Column under_test(&column_metadata);

   // Nulls appear in every chunk, including at the start of a chunk that is not the first one. Each
   // chunk's global row ids begin at a fresh 2^16-aligned offset (chunk k starts at k << 16), so a
   // missing per-chunk offset would land the null bitmap on the wrong global row ids.
   using rhydb::storage::column::RowId;
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2020-01-01", ""})).has_value());
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"", "2021-03-21", ""})).has_value());
   RHYDB_ASSERT(under_test.appendChunk(chunkOf({"2022-01-01"})).has_value());

   ASSERT_EQ(under_test.numChunks(), 3);
   ASSERT_EQ(under_test.chunkSize(0), 2);
   ASSERT_EQ(under_test.chunkSize(1), 3);
   ASSERT_EQ(under_test.chunkSize(2), 1);

   // {chunk_id, row_in_chunk} -> expected null state.
   const std::vector<std::pair<RowId, bool>> expected_null{
      {{.chunk_id = 0, .row_in_chunk = 0}, false},
      {{.chunk_id = 0, .row_in_chunk = 1}, true},
      {{.chunk_id = 1, .row_in_chunk = 0}, true},
      {{.chunk_id = 1, .row_in_chunk = 1}, false},
      {{.chunk_id = 1, .row_in_chunk = 2}, true},
      {{.chunk_id = 2, .row_in_chunk = 0}, false},
   };
   for (const auto& [row_id, is_null] : expected_null) {
      ASSERT_EQ(under_test.isNull(row_id), is_null) << "row_id=" << row_id.toGlobal();
   }

   ASSERT_EQ(
      rhydb::common::date32ToString(under_test.getValue(RowId{.chunk_id = 1, .row_in_chunk = 1})),
      "2021-03-21"
   );
   ASSERT_EQ(
      rhydb::common::date32ToString(under_test.getValue(RowId{.chunk_id = 2, .row_in_chunk = 0})),
      "2022-01-01"
   );
}
