#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "rhydb/common/bitmap.h"

using rhydb::storage::column::DictionaryEncodedColumn;
using rhydb::storage::column::DictionaryEncodedColumnMetadata;
using rhydb::storage::column::RowId;

namespace {
// Buffers the values into a chunk and appends it to the column.
[[nodiscard]] std::expected<void, std::string> appendValues(
   DictionaryEncodedColumn& column,
   std::initializer_list<std::string_view> values
) {
   DictionaryEncodedColumn::Builder builder;
   for (const auto& value : values) {
      builder.insert(value);
   }
   return column.appendChunk(builder.finalize());
}
}  // namespace

// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST(DictionaryEncodedColumn, shouldReturnTheCorrectFilteredValues) {
   DictionaryEncodedColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(
      appendValues(under_test, {"value 1", "value 2", "value 2", "value 3", "value 1"}).has_value()
   );

   const auto result1 = under_test.filter("value 1");
   ASSERT_EQ(*result1.value(), roaring::Roaring({0, 4}));

   const auto result2 = under_test.filter("value 2");
   ASSERT_EQ(*result2.value(), roaring::Roaring({1, 2}));

   const auto result3 = under_test.filter("value that does not exist");
   ASSERT_EQ(result3, std::nullopt);
}

TEST(DictionaryEncodedColumn, insertValuesToPartition) {
   DictionaryEncodedColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(
      appendValues(under_test, {"value 1", "value 2", "value 2", "value 3", "value 1"}).has_value()
   );

   EXPECT_EQ(under_test.getValue(RowId(0, 0)), 0U);
   EXPECT_EQ(under_test.getValue(RowId(0, 1)), 1U);
   EXPECT_EQ(under_test.getValue(RowId(0, 2)), 1U);
   EXPECT_EQ(under_test.getValue(RowId(0, 3)), 2U);
   EXPECT_EQ(under_test.getValue(RowId(0, 4)), 0U);

   EXPECT_EQ(under_test.lookupValue(0U), "value 1");
   EXPECT_EQ(under_test.lookupValue(1U), "value 2");
   EXPECT_EQ(under_test.lookupValue(2U), "value 3");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DictionaryEncodedColumn, valuesSpanningMultipleAppendedChunks) {
   DictionaryEncodedColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn under_test{&column_metadata};

   // Each appendChunk starts a fresh, immutable chunk of value ids whose global row ids begin at a
   // fresh 2^16-aligned offset (chunk k starts at k << 16), while the inverted index accumulates
   // those aligned global row ids across chunk boundaries.
   ASSERT_TRUE(appendValues(under_test, {"value 1", "value 2"}).has_value());
   ASSERT_TRUE(appendValues(under_test, {"value 2", "value 3"}).has_value());
   ASSERT_TRUE(appendValues(under_test, {"value 1"}).has_value());

   ASSERT_EQ(under_test.numChunks(), 3);
   ASSERT_EQ(under_test.chunkSize(0), 2);
   ASSERT_EQ(under_test.chunkSize(1), 2);
   ASSERT_EQ(under_test.chunkSize(2), 1);
   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "value 1");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "value 2");
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "value 2");
   EXPECT_EQ(under_test.getValueString(RowId(1, 1)), "value 3");
   EXPECT_EQ(under_test.getValueString(RowId(2, 0)), "value 1");

   ASSERT_EQ(
      *under_test.filter("value 1").value(),
      roaring::Roaring({RowId::chunkStart(0) + 0, RowId::chunkStart(2) + 0})
   );
   ASSERT_EQ(
      *under_test.filter("value 2").value(),
      roaring::Roaring({RowId::chunkStart(0) + 1, RowId::chunkStart(1) + 0})
   );
   ASSERT_EQ(*under_test.filter("value 3").value(), roaring::Roaring({RowId::chunkStart(1) + 1}));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DictionaryEncodedColumn, updateKeepsInvertedIndexConsistent) {
   DictionaryEncodedColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(
      appendValues(under_test, {"value 1", "value 2", "value 2", "value 3", "value 1"}).has_value()
   );

   // Reassign the two "value 2" rows to a value not yet in the dictionary; the old value's bitmap
   // empties and the new value is interned in both the dictionary and the inverted index.
   under_test.update(roaring::Roaring({1, 2}), "value 4");
   ASSERT_EQ(*under_test.filter("value 4").value(), roaring::Roaring({1, 2}));
   ASSERT_EQ(*under_test.filter("value 2").value(), roaring::Roaring());
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "value 4");
   EXPECT_EQ(under_test.getValueString(RowId(0, 2)), "value 4");

   // A nullopt update detaches rows from their value bitmap and marks them null.
   under_test.update(roaring::Roaring({0}), std::nullopt);
   ASSERT_EQ(*under_test.filter("value 1").value(), roaring::Roaring({4}));
   ASSERT_EQ(*under_test.filter(std::optional<std::string>{}).value(), roaring::Roaring({0}));
   EXPECT_TRUE(under_test.isNull(RowId(0, 0)));

   // A previously-null row can be reassigned to a concrete value again.
   under_test.update(roaring::Roaring({0}), "value 1");
   ASSERT_EQ(*under_test.filter("value 1").value(), roaring::Roaring({0, 4}));
   EXPECT_FALSE(under_test.isNull(RowId(0, 0)));
}

TEST(DictionaryEncodedColumn, updateChunkAssignsEachRowItsOwnValue) {
   DictionaryEncodedColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(appendValues(under_test, {"value 1", "value 2", "value 2", "value 3"}).has_value());

   // The i-th updated row gets the i-th value; the inverted index follows every row to its value.
   const roaring::Roaring rows{0, 1, 2};
   for (const auto& [chunk_id, chunk_rows] : rhydb::Bitmap{&rows}) {
      under_test.update(chunk_id, chunk_rows, {"value 3", std::nullopt, "value 4"});
   }

   ASSERT_EQ(*under_test.filter("value 1").value(), roaring::Roaring());
   ASSERT_EQ(*under_test.filter("value 2").value(), roaring::Roaring());
   ASSERT_EQ(*under_test.filter("value 3").value(), roaring::Roaring({0, 3}));
   ASSERT_EQ(*under_test.filter("value 4").value(), roaring::Roaring({2}));
   EXPECT_TRUE(under_test.isNull(RowId(0, 1)));
}

// NOLINTEND(bugprone-unchecked-optional-access)
