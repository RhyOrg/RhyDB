#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using rhydb::common::LineageTreeAndIdMap;
using rhydb::common::RecombinantEdgeFollowingMode;
using rhydb::preprocessing::LineageDefinitionFile;
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

TEST(DictionaryEncodedColumn, addingLineageAndThenSublineageFiltersCorrectly) {
   auto lineage_definition = LineageTreeAndIdMap::fromLineageDefinitionFilePath(
      "testBaseData/exampleDataset/lineage_definition.yaml"
   );
   DictionaryEncodedColumnMetadata column_metadata("some_column", lineage_definition, false);
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(
      appendValues(under_test, {"BA.1.1", "BA.1.1", "BA.1.1.1", "BA.1.1.1.1", "BA.1.1"}).has_value()
   );

   EXPECT_EQ(*under_test.filter({"BA.1.1"}).value(), roaring::Roaring({0, 1, 4}));
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("BA.1.1").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({0, 1, 2, 3, 4})
   );

   EXPECT_EQ(*under_test.filter({"BA.1.1.1"}).value(), roaring::Roaring({2}));
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("BA.1.1.1").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({2, 3})
   );
}

TEST(DictionaryEncodedColumn, addingSublineageAndThenLineageFiltersCorrectly) {
   auto lineage_definition = LineageTreeAndIdMap::fromLineageDefinitionFilePath(
      "testBaseData/exampleDataset/lineage_definition.yaml"
   );
   DictionaryEncodedColumnMetadata column_metadata("some_column", lineage_definition, false);
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(
      appendValues(under_test, {"BA.1.1.1", "BA.1.1.1", "BA.1", "BA.1.1", "BA.1.1.1"}).has_value()
   );

   EXPECT_EQ(*under_test.filter({"BA.1.1"}).value(), roaring::Roaring({3}));
   EXPECT_EQ(under_test.filter({"B.1.1.529.1.1"}), std::nullopt);
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterExcludingSublineages(under_test.getValueId("B.1.1.529.1.1").value())
          .value(),
      roaring::Roaring({3})
   );
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("BA.1.1").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({0, 1, 3, 4})
   );

   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("BA.1.1.1").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({0, 1, 4})
   );
}

TEST(DictionaryEncodedColumn, queryParentLineageThatWasNeverInserted) {
   auto lineage_definition = LineageTreeAndIdMap::fromLineageDefinitionFilePath(
      "testBaseData/exampleDataset/lineage_definition.yaml"
   );
   DictionaryEncodedColumnMetadata column_metadata("some_column", lineage_definition, false);
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(appendValues(under_test, {"BA.1.1.1", "BA.1.1.1", "BA.2", "BA.1.1"}).has_value());

   EXPECT_EQ(
      under_test.getLineageIndex()->filterExcludingSublineages(
         under_test.getValueId("BA.1").value()
      ),
      std::nullopt
   );
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("BA.1").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({0, 1, 3})
   );
}

TEST(DictionaryEncodedColumn, errorWhenInsertingIncorrectLineages) {
   auto lineage_definition =
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
A: {}
A.1:
  parents: ["A"]
)"));
   DictionaryEncodedColumnMetadata column_metadata("some_column", lineage_definition, false);
   DictionaryEncodedColumn under_test{&column_metadata};
   ASSERT_TRUE(appendValues(under_test, {"A"}).has_value());
   auto success = appendValues(under_test, {"A.2"});
   ASSERT_FALSE(success.has_value());
   ASSERT_EQ(
      success.error(),
      "The value 'A.2' is not a valid lineage value for column 'some_column'. "
      "Is your lineage definition file outdated?"
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DictionaryEncodedColumn, failingChunkDoesNotMutatePreviouslyAppendedChunks) {
   auto lineage_definition =
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
A: {}
A.1:
  parents: ["A"]
)"));
   DictionaryEncodedColumnMetadata column_metadata("some_column", lineage_definition, false);
   DictionaryEncodedColumn under_test{&column_metadata};

   ASSERT_TRUE(appendValues(under_test, {"A", "A.1", "A"}).has_value());

   // This chunk contains a valid value ("A.1") followed by an invalid one ("A.2"). The whole
   // chunk must be rejected without mutating any state from the previously appended chunk.
   auto failure = appendValues(under_test, {"A.1", "A.2"});
   ASSERT_FALSE(failure.has_value());

   // The failed chunk must not have grown the column nor added any of its rows to the index.
   ASSERT_EQ(under_test.numChunks(), 1);
   ASSERT_EQ(under_test.chunkSize(0), 3);
   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "A");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "A.1");
   EXPECT_EQ(under_test.getValueString(RowId(0, 2)), "A");

   EXPECT_EQ(*under_test.filter({"A"}).value(), roaring::Roaring({0, 2}));
   EXPECT_EQ(*under_test.filter({"A.1"}).value(), roaring::Roaring({1}));
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("A").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({0, 1, 2})
   );

   // A subsequent valid chunk must continue numbering rows from where the successful chunk ended,
   // i.e. the failed chunk left no gaps in the row ids.
   ASSERT_TRUE(appendValues(under_test, {"A.1"}).has_value());
   ASSERT_EQ(under_test.numChunks(), 2);
   ASSERT_EQ(under_test.chunkSize(0), 3);
   ASSERT_EQ(under_test.chunkSize(1), 1);
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "A.1");
   EXPECT_EQ(*under_test.filter({"A.1"}).value(), roaring::Roaring({1, RowId(1, 0).toGlobal()}));
}

TEST(DictionaryEncodedColumn, ignoringErrorWhenInsertingIncorrectLineagesIfSpecified) {
   auto lineage_definition =
      LineageTreeAndIdMap::fromLineageDefinitionFile(LineageDefinitionFile::fromYAMLString(R"(
A: {}
A.1:
  parents: ["A"]
)"));
   DictionaryEncodedColumnMetadata column_metadata("some_column", lineage_definition, true);
   DictionaryEncodedColumn under_test{&column_metadata};
   ASSERT_TRUE(appendValues(under_test, {"A", "not in the lineage hierarchy"}).has_value());
   EXPECT_EQ(
      *under_test.getLineageIndex()
          ->filterIncludingSublineages(
             under_test.getValueId("A").value(), RecombinantEdgeFollowingMode::DO_NOT_FOLLOW
          )
          .value(),
      roaring::Roaring({0})
   );
}

// NOLINTEND(bugprone-unchecked-optional-access)
