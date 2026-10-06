#include "rhydb/storage/column/int_column.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <roaring/roaring.hh>

using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::Int32Column;
using rhydb::storage::column::Int64Column;
using rhydb::storage::column::RowId;

namespace {

template <typename ColumnT>
class NumericColumnTest : public ::testing::Test {};

}  // namespace

using NumericColumnTypes = ::testing::Types<Int32Column, Int64Column>;
TYPED_TEST_SUITE(NumericColumnTest, NumericColumnTypes);

TYPED_TEST(NumericColumnTest, doesNotErrorOnValidInputs) {
   ColumnMetadata column_metadata("int_column1");
   TypeParam column{&column_metadata};
   typename TypeParam::Builder builder;
   builder.insert(123);
   builder.insertNull();
   RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());
   ASSERT_EQ(column.numChunks(), 1);
   ASSERT_EQ(column.chunkSize(0), 2);
   ASSERT_FALSE(column.isNull(RowId(0, 0)));
   ASSERT_EQ(column.getValue(RowId(0, 0)), 123);
   ASSERT_TRUE(column.isNull(RowId(0, 1)));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(NumericColumnTest, updateAssignsScalarValueAcrossChunks) {
   ColumnMetadata column_metadata("int_column1");
   TypeParam column{&column_metadata};

   typename TypeParam::Builder chunk0;
   chunk0.insert(10);
   chunk0.insertNull();  // (0, 1)
   chunk0.insert(30);
   RHYDB_ASSERT(column.appendChunk(chunk0.finalize()).has_value());

   typename TypeParam::Builder chunk1;
   chunk1.insert(40);
   chunk1.insert(50);
   RHYDB_ASSERT(column.appendChunk(chunk1.finalize()).has_value());

   ASSERT_EQ(column.numChunks(), 2);

   // Assign a single scalar to a set of rows spanning both column chunks, including the previously
   // null row (0, 1), which becomes non-null.
   roaring::Roaring row_ids;
   row_ids.add(RowId(0, 1).toGlobal());
   row_ids.add(RowId(0, 2).toGlobal());
   row_ids.add(RowId(1, 0).toGlobal());
   column.update(row_ids, 77);

   ASSERT_FALSE(column.isNull(RowId(0, 0)));
   ASSERT_EQ(column.getValue(RowId(0, 0)), 10);  // untouched
   ASSERT_FALSE(column.isNull(RowId(0, 1)));
   ASSERT_EQ(column.getValue(RowId(0, 1)), 77);  // null -> 77
   ASSERT_FALSE(column.isNull(RowId(0, 2)));
   ASSERT_EQ(column.getValue(RowId(0, 2)), 77);
   ASSERT_FALSE(column.isNull(RowId(1, 0)));
   ASSERT_EQ(column.getValue(RowId(1, 0)), 77);
   ASSERT_FALSE(column.isNull(RowId(1, 1)));
   ASSERT_EQ(column.getValue(RowId(1, 1)), 50);  // untouched

   // A nullopt update clears the rows.
   roaring::Roaring to_clear;
   to_clear.add(RowId(0, 2).toGlobal());
   column.update(to_clear, std::nullopt);
   ASSERT_TRUE(column.isNull(RowId(0, 2)));
   ASSERT_FALSE(column.isNull(RowId(1, 0)));
   ASSERT_EQ(column.getValue(RowId(1, 0)), 77);
}

TEST(Int64Column, storesValuesBeyondInt32Range) {
   ColumnMetadata column_metadata("int64_column1");
   Int64Column column{&column_metadata};
   Int64Column::Builder builder;
   const int64_t large_positive = 5'000'000'000LL;   // > INT32_MAX
   const int64_t large_negative = -5'000'000'000LL;  // < INT32_MIN
   builder.insert(large_positive);
   builder.insert(large_negative);
   builder.insertNull();
   RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());
   ASSERT_EQ(column.getValue(RowId(0, 0)), large_positive);
   ASSERT_EQ(column.getValue(RowId(0, 1)), large_negative);
   ASSERT_TRUE(column.isNull(RowId(0, 2)));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(Int64Column, updateAssignsValueBeyondInt32Range) {
   ColumnMetadata column_metadata("int64_column1");
   Int64Column column{&column_metadata};

   Int64Column::Builder chunk0;
   chunk0.insert(10);
   chunk0.insertNull();  // (0, 1)
   RHYDB_ASSERT(column.appendChunk(chunk0.finalize()).has_value());

   const int64_t large_value = 9'000'000'000LL;  // > INT32_MAX
   roaring::Roaring row_ids;
   row_ids.add(RowId(0, 1).toGlobal());
   column.update(row_ids, large_value);

   ASSERT_FALSE(column.isNull(RowId(0, 0)));
   ASSERT_EQ(column.getValue(RowId(0, 0)), 10);  // untouched
   ASSERT_FALSE(column.isNull(RowId(0, 1)));
   ASSERT_EQ(column.getValue(RowId(0, 1)), large_value);  // null -> value
}
