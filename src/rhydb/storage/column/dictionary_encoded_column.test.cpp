#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "rhydb/common/bitmap.h"

using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::DictionaryEncodedColumn;
using rhydb::storage::column::RowId;
using rhydb::storage::column::StringColumn;

namespace {
// Buffers the values into a chunk and appends it to the column.
[[nodiscard]] std::expected<void, std::string> appendValues(
   DictionaryEncodedColumn<StringColumn>& column,
   std::initializer_list<std::string_view> values
) {
   DictionaryEncodedColumn<StringColumn>::Builder builder;
   for (const auto& value : values) {
      builder.insert(value);
   }
   return column.appendChunk(builder.finalize());
}
}  // namespace

// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST(DictionaryEncodedColumn, shouldReturnTheCorrectFilteredValues) {
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

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
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

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
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

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
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

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
   ASSERT_EQ(
      rhydb::Bitmap::fromChunkContainers(under_test.null_bitmap).toRoaring(), roaring::Roaring({0})
   );
   EXPECT_TRUE(under_test.isNull(RowId(0, 0)));

   // A previously-null row can be reassigned to a concrete value again.
   under_test.update(roaring::Roaring({0}), "value 1");
   ASSERT_EQ(*under_test.filter("value 1").value(), roaring::Roaring({0, 4}));
   EXPECT_FALSE(under_test.isNull(RowId(0, 0)));
}

TEST(DictionaryEncodedColumn, updateChunkAssignsEachRowItsOwnValue) {
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DictionaryEncodedColumn, dictionaryStaysSortedWhenSmallerValuesArriveLater) {
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

   ASSERT_TRUE(appendValues(under_test, {"c", "e", "c"}).has_value());
   EXPECT_EQ(under_test.getValueId("c"), 0U);
   EXPECT_EQ(under_test.getValueId("e"), 1U);

   // "a" and "d" sort before existing values, so the ids of those shift and are remapped in place.
   ASSERT_TRUE(appendValues(under_test, {"d", "a", "e"}).has_value());
   ASSERT_EQ(under_test.numDictionaryValues(), 4);
   EXPECT_EQ(under_test.lookupValue(0U), "a");
   EXPECT_EQ(under_test.lookupValue(1U), "c");
   EXPECT_EQ(under_test.lookupValue(2U), "d");
   EXPECT_EQ(under_test.lookupValue(3U), "e");
   EXPECT_EQ(under_test.getValue(RowId(0, 0)), 1U);
   EXPECT_EQ(under_test.getValue(RowId(0, 1)), 3U);
   EXPECT_EQ(under_test.getValue(RowId(1, 1)), 0U);
   EXPECT_EQ(under_test.getValueString(RowId(0, 2)), "c");
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "d");

   ASSERT_EQ(*under_test.filter("c").value(), roaring::Roaring({0, 2}));
   ASSERT_EQ(*under_test.filter("e").value(), roaring::Roaring({1, RowId::chunkStart(1) + 2}));
   ASSERT_EQ(*under_test.filter("a").value(), roaring::Roaring({RowId::chunkStart(1) + 1}));

   // An update introducing a new smallest value remaps as well.
   under_test.update(roaring::Roaring({0}), "Z long value exceeding the short string size");
   EXPECT_EQ(under_test.lookupValue(0U), "Z long value exceeding the short string size");
   EXPECT_EQ(
      under_test.getValueString(RowId(0, 0)), "Z long value exceeding the short string size"
   );
   EXPECT_EQ(under_test.getValueString(RowId(0, 2)), "c");
   ASSERT_EQ(*under_test.filter("c").value(), roaring::Roaring({2}));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DictionaryEncodedColumn, dictionarySpanningMultipleChunks) {
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

   constexpr size_t CHUNK_SIZE = rhydb::storage::column::COLUMN_CHUNK_SIZE;
   constexpr size_t NUM_VALUES = CHUNK_SIZE + 10;
   // Descending values, flushed at the data chunk limit: the second data chunk interns the ten
   // smallest values, which shifts every id stored for the first data chunk past the boundary of
   // the dictionary's first chunk.
   DictionaryEncodedColumn<StringColumn>::Builder builder;
   for (size_t i = NUM_VALUES; i > 0; --i) {
      builder.insert(fmt::format("value {:06}", i - 1));
      if (builder.numValues() == CHUNK_SIZE) {
         ASSERT_TRUE(under_test.appendChunk(builder.finalize()).has_value());
      }
   }
   ASSERT_TRUE(under_test.appendChunk(builder.finalize()).has_value());

   ASSERT_EQ(under_test.numChunks(), 2);
   ASSERT_EQ(under_test.chunkSize(0), CHUNK_SIZE);
   ASSERT_EQ(under_test.chunkSize(1), 10);
   ASSERT_EQ(under_test.numDictionaryValues(), NUM_VALUES);
   for (const size_t value_id : {size_t{0}, size_t{65535}, size_t{65536}, NUM_VALUES - 1}) {
      const auto value = fmt::format("value {:06}", value_id);
      EXPECT_EQ(under_test.getValueId(value), value_id);
      EXPECT_EQ(under_test.lookupValue(static_cast<rhydb::Idx>(value_id)), value);
   }

   EXPECT_EQ(under_test.getValue(RowId(0, 0)), NUM_VALUES - 1);
   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), fmt::format("value {:06}", NUM_VALUES - 1));
   EXPECT_EQ(under_test.getValue(RowId(0, UINT16_MAX)), 10U);
   EXPECT_EQ(under_test.getValueString(RowId(0, UINT16_MAX)), "value 000010");
   EXPECT_EQ(under_test.getValue(RowId(1, 0)), 9U);
   EXPECT_EQ(under_test.getValueString(RowId(1, 9)), "value 000000");

   ASSERT_EQ(*under_test.filter("value 000010").value(), roaring::Roaring({UINT16_MAX}));
   ASSERT_EQ(
      *under_test.filter("value 000000").value(), roaring::Roaring({RowId::chunkStart(1) + 9})
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DictionaryEncodedColumn, nullArrivingLaterKeepsNullAndEmptyStringApart) {
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> under_test{&column_metadata};

   ASSERT_TRUE(appendValues(under_test, {"b", "c"}).has_value());

   // The first null interns the "" placeholder, which sorts before every existing value and so
   // shifts all their ids. A real "" shares that id, but only it is in the inverted index.
   DictionaryEncodedColumn<StringColumn>::Builder builder;
   builder.insertNull();
   builder.insert("");
   builder.insert("a");
   ASSERT_TRUE(under_test.appendChunk(builder.finalize()).has_value());

   ASSERT_EQ(under_test.numDictionaryValues(), 4);
   EXPECT_EQ(under_test.getValue(RowId(0, 0)), 2U);
   EXPECT_EQ(under_test.getValue(RowId(0, 1)), 3U);
   EXPECT_EQ(under_test.getValue(RowId(1, 0)), 0U);
   EXPECT_EQ(under_test.getValue(RowId(1, 1)), 0U);
   EXPECT_EQ(under_test.getValue(RowId(1, 2)), 1U);

   ASSERT_EQ(*under_test.filter("b").value(), roaring::Roaring({0}));
   ASSERT_EQ(*under_test.filter("c").value(), roaring::Roaring({1}));
   ASSERT_EQ(*under_test.filter("a").value(), roaring::Roaring({RowId::chunkStart(1) + 2}));
   ASSERT_EQ(*under_test.filter("").value(), roaring::Roaring({RowId::chunkStart(1) + 1}));
   ASSERT_EQ(
      rhydb::Bitmap::fromChunkContainers(under_test.null_bitmap).toRoaring(),
      roaring::Roaring({RowId::chunkStart(1) + 0})
   );
   EXPECT_TRUE(under_test.isNull(RowId(1, 0)));
   EXPECT_FALSE(under_test.isNull(RowId(1, 1)));
}

TEST(DictionaryEncodedColumn, columnsSharingMetadataKeepTheirOwnDictionaries) {
   ColumnMetadata column_metadata("some_column");
   DictionaryEncodedColumn<StringColumn> first{&column_metadata};
   DictionaryEncodedColumn<StringColumn> second{&column_metadata};

   // Interning "a" into `second` must not renumber the id `first` stores for "c".
   ASSERT_TRUE(appendValues(first, {"c"}).has_value());
   ASSERT_TRUE(appendValues(second, {"a"}).has_value());

   EXPECT_EQ(first.getValueString(RowId(0, 0)), "c");
   ASSERT_EQ(*first.filter("c").value(), roaring::Roaring({0}));
   EXPECT_EQ(first.filter("a"), std::nullopt);
   EXPECT_EQ(second.getValueString(RowId(0, 0)), "a");
}

// NOLINTEND(bugprone-unchecked-optional-access)
