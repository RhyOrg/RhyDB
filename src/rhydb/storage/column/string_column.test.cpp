#include "rhydb/storage/column/string_column.h"

#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <roaring/roaring.hh>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>

#include <gtest/gtest.h>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/common/tree_node_id.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/update_chunks.h"

using rhydb::storage::column::RowId;
using rhydb::storage::column::StringColumn;
using rhydb::storage::column::StringColumnMetadata;

namespace {
// Buffers the values into a chunk and appends it to the column.
[[nodiscard]] std::expected<void, std::string> appendStringValues(
   StringColumn& column,
   std::initializer_list<std::string_view> values
) {
   StringColumn::Builder builder;
   for (const auto& value : values) {
      builder.insert(value);
   }
   return column.appendChunk(builder.finalize());
}
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, rawInsertedValuesRequeried) {
   StringColumnMetadata metadata{"string_column"};
   StringColumn under_test(&metadata);

   RHYDB_ASSERT(appendStringValues(
                   under_test,
                   {"value 1",
                    "value 2",
                    "value 2",
                    "value 3",
                    "some string that is a little longer 1",
                    "value 1"}
   )
                   .has_value());

   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "value 1");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "value 2");
   EXPECT_EQ(under_test.getValueString(RowId(0, 2)), "value 2");
   EXPECT_EQ(under_test.getValueString(RowId(0, 3)), "value 3");
   EXPECT_EQ(under_test.getValueString(RowId(0, 4)), "some string that is a little longer 1");
   EXPECT_EQ(under_test.getValueString(RowId(0, 5)), "value 1");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, serializationOfMetadataWorks) {
   auto phylo_tree = rhydb::common::PhyloTree::fromNewickString(
      "((CHILD2:0.5, CHILD3:1)CHILD:0.1, NOT_IN_DATASET:1.5)ROOT;"
   );
   StringColumnMetadata metadata{"string_column", std::move(phylo_tree)};
   StringColumn column(&metadata);

   RHYDB_ASSERT(appendStringValues(column, {"CHILD2", "CHILD3", "NOT_IN_TREE"}).has_value());

   std::ostringstream oss;
   boost::archive::binary_oarchive oarchive(oss);
   oarchive << metadata;

   std::istringstream iss(oss.str());
   boost::archive::binary_iarchive iarchive(iss);
   std::shared_ptr<StringColumnMetadata> under_test;
   iarchive >> under_test;

   auto node_dict = under_test->phylo_tree.value().nodes;

   EXPECT_EQ(node_dict.size(), 5);
   EXPECT_EQ(
      node_dict.at(rhydb::common::TreeNodeId{"CHILD2"})->parent, rhydb::common::TreeNodeId{"CHILD"}
   );
   EXPECT_EQ(node_dict.at(rhydb::common::TreeNodeId{"CHILD2"})->row_index, 0);
   EXPECT_EQ(
      node_dict.at(rhydb::common::TreeNodeId{"CHILD3"})->parent, rhydb::common::TreeNodeId{"CHILD"}
   );
   EXPECT_EQ(node_dict.at(rhydb::common::TreeNodeId{"CHILD3"})->row_index, 1);
   EXPECT_EQ(
      node_dict.at(rhydb::common::TreeNodeId{"CHILD"})->parent, rhydb::common::TreeNodeId{"ROOT"}
   );
   EXPECT_EQ(node_dict.at(rhydb::common::TreeNodeId{"CHILD"})->row_index, std::nullopt);
   EXPECT_EQ(
      node_dict.at(rhydb::common::TreeNodeId{"NOT_IN_DATASET"})->parent,
      rhydb::common::TreeNodeId{"ROOT"}
   );
   EXPECT_EQ(node_dict.at(rhydb::common::TreeNodeId{"NOT_IN_DATASET"})->row_index, std::nullopt);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, rawInsertedValuesWithPhyloTreeRequeried) {
   auto phylo_tree = rhydb::common::PhyloTree::fromNewickString(
      "((CHILD2:0.5, CHILD3:1)CHILD:0.1, NOT_IN_DATASET:1.5)ROOT;"
   );
   StringColumnMetadata metadata{"string_column", std::move(phylo_tree)};
   StringColumn under_test(&metadata);

   RHYDB_ASSERT(appendStringValues(under_test, {"CHILD2", "CHILD3", "NOT_IN_TREE"}).has_value());

   auto tree_node_id_child = metadata.phylo_tree->getTreeNodeId("CHILD");
   auto tree_node_id_child2 = metadata.phylo_tree->getTreeNodeId("CHILD2");
   auto tree_node_id_child3 = metadata.phylo_tree->getTreeNodeId("CHILD3");
   auto tree_node_id_not_in_tree = metadata.phylo_tree->getTreeNodeId("NOT_IN_TREE");
   auto tree_node_id_root = metadata.phylo_tree->getTreeNodeId("ROOT");

   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "CHILD2");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "CHILD3");
   EXPECT_EQ(under_test.getValueString(RowId(0, 2)), "NOT_IN_TREE");
   EXPECT_EQ(under_test.getDescendants(tree_node_id_child2.value()).cardinality(), 0);
   EXPECT_EQ(under_test.getDescendants(tree_node_id_child.value()).cardinality(), 2);
   EXPECT_EQ(under_test.getDescendants(tree_node_id_root.value()).cardinality(), 2);

   EXPECT_EQ(tree_node_id_not_in_tree, std::nullopt);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, duplicatePhyloLeafLeavesColumnUnmodified) {
   auto phylo_tree = rhydb::common::PhyloTree::fromNewickString(
      "((CHILD2:0.5, CHILD3:1)CHILD:0.1, NOT_IN_DATASET:1.5)ROOT;"
   );
   StringColumnMetadata metadata{"string_column", std::move(phylo_tree)};
   StringColumn under_test(&metadata);

   // First append a valid leaf so we have committed state that must survive the failed append.
   RHYDB_ASSERT(appendStringValues(under_test, {"CHILD2"}).has_value());

   // A buffer that re-uses CHILD2 (already committed) with a null in between must fail, and must
   // not leave the null in null_bitmap or grow the column.
   StringColumn::Builder builder;
   builder.insert("CHILD3");
   builder.insertNull();
   builder.insert("CHILD2");
   auto result = under_test.appendChunk(builder.finalize());

   ASSERT_FALSE(result.has_value());
   EXPECT_EQ(under_test.numChunks(), 1);
   EXPECT_EQ(under_test.chunkSize(0), 1);
   EXPECT_TRUE(rhydb::Bitmap::fromChunkContainers(under_test.null_bitmap).isEmpty());
   // CHILD3 must not have been bound, since the whole buffer was rejected.
   EXPECT_EQ(
      metadata.phylo_tree->nodes.at(rhydb::common::TreeNodeId{"CHILD3"})->row_index, std::nullopt
   );
   EXPECT_EQ(metadata.phylo_tree->nodes.at(rhydb::common::TreeNodeId{"CHILD2"})->row_index, 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, duplicatePhyloLeafWithinSingleBufferIsRejected) {
   auto phylo_tree = rhydb::common::PhyloTree::fromNewickString(
      "((CHILD2:0.5, CHILD3:1)CHILD:0.1, NOT_IN_DATASET:1.5)ROOT;"
   );
   StringColumnMetadata metadata{"string_column", std::move(phylo_tree)};
   StringColumn under_test(&metadata);

   // The same leaf appears twice within one buffer; rowIndexExists() cannot catch this, so the
   // in-buffer claim tracking must.
   auto result = appendStringValues(under_test, {"CHILD2", "CHILD2"});

   ASSERT_FALSE(result.has_value());
   EXPECT_EQ(under_test.numChunks(), 0);
   EXPECT_EQ(
      metadata.phylo_tree->nodes.at(rhydb::common::TreeNodeId{"CHILD2"})->row_index, std::nullopt
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, rawInsertedValuesRequeryLongValue) {
   StringColumnMetadata column("string_column");
   StringColumn under_test{&column};

   RHYDB_ASSERT(appendStringValues(
                   under_test,
                   {"value 1",
                    "value 2",
                    "value 2",
                    "value 3",
                    "some string that is a little longer 1",
                    "value 1"}
   )
                   .has_value());

   EXPECT_EQ(under_test.getValueString(RowId(0, 4)), "some string that is a little longer 1");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, compareAcrossColumns) {
   StringColumnMetadata under_test("string_column");
   StringColumn column_1{&under_test};
   RHYDB_ASSERT(appendStringValues(
                   column_1,
                   {"value 1",
                    "value 2",
                    "value 2",
                    "value 3",
                    "some string that is a little longer 1",
                    "value 1"}
   )
                   .has_value());

   StringColumn column_2{&under_test};
   RHYDB_ASSERT(appendStringValues(
                   column_2,
                   {"other value 2",
                    "other values 3",
                    "value 1",
                    "other value 3",
                    "some string that is a little longer 1",
                    "other value 1"}
   )
                   .has_value());

   EXPECT_EQ(column_1.getValueString(RowId(0, 0)), column_1.getValueString(RowId(0, 5)));
   EXPECT_EQ(column_1.getValueString(RowId(0, 5)), column_2.getValueString(RowId(0, 2)));
   EXPECT_EQ(column_1.getValueString(RowId(0, 4)), column_2.getValueString(RowId(0, 4)));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, updateRebuildsTouchedChunks) {
   StringColumnMetadata metadata{"string_column"};
   StringColumn under_test(&metadata);

   RHYDB_ASSERT(appendStringValues(under_test, {"short a", "value to overwrite"}).has_value());
   RHYDB_ASSERT(appendStringValues(under_test, {"short b", "short c"}).has_value());

   // Overwrite a short value with a long one (moving it into the variable-data registry) and a long
   // value with a short one, targeting rows in both chunks at once. Untouched rows in a rebuilt
   // chunk must keep their original values.
   roaring::Roaring to_long;
   to_long.add(RowId(0, 0).toGlobal());
   to_long.add(RowId(1, 1).toGlobal());
   under_test.update(to_long, "a replacement value that is definitely long");

   roaring::Roaring to_short;
   to_short.add(RowId(0, 1).toGlobal());
   under_test.update(to_short, "tiny");

   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "a replacement value that is definitely long");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "tiny");
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "short b");  // untouched
   EXPECT_EQ(under_test.getValueString(RowId(1, 1)), "a replacement value that is definitely long");

   // A nullopt update marks rows null; a concrete value clears the null flag again.
   roaring::Roaring to_null;
   to_null.add(RowId(1, 0).toGlobal());
   under_test.update(to_null, std::nullopt);
   EXPECT_TRUE(under_test.isNull(RowId(1, 0)));

   under_test.update(to_null, "revived");
   EXPECT_FALSE(under_test.isNull(RowId(1, 0)));
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "revived");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, updateChunksAssignsEachRowItsOwnValue) {
   StringColumnMetadata metadata{"string_column"};
   StringColumn under_test(&metadata);

   RHYDB_ASSERT(appendStringValues(under_test, {"a", "b", "c"}).has_value());
   RHYDB_ASSERT(appendStringValues(under_test, {"d", "e"}).has_value());

   // The values of each chunk are given for its updated rows, in ascending order.
   const roaring::Roaring rows{
      RowId(0, 0).toGlobal(),
      RowId(0, 2).toGlobal(),
      RowId(1, 1).toGlobal(),
   };
   std::vector<std::pair<uint16_t, rhydb::roaring_util::RoaringContainerView>> chunks;
   for (const auto& chunk : rhydb::Bitmap{&rows}) {
      chunks.push_back(chunk);
   }
   rhydb::storage::column::updateChunks(
      under_test,
      chunks,
      {{"first", std::nullopt}, {"a value that is long enough to not be stored inline"}}
   );

   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "first");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "b");  // untouched
   EXPECT_TRUE(under_test.isNull(RowId(0, 2)));
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "d");  // untouched
   EXPECT_EQ(
      under_test.getValueString(RowId(1, 1)), "a value that is long enough to not be stored inline"
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, valuesSpanningMultipleAppendedChunks) {
   StringColumnMetadata metadata{"string_column"};
   StringColumn under_test(&metadata);

   // Each appendChunk starts a fresh, immutable chunk whose row ids begin at a fresh 2^16-aligned
   // offset (chunk k starts at k << 16). Both short (in-place) and long (suffix in variable data)
   // values must resolve to their own chunk's registries.
   RHYDB_ASSERT(
      appendStringValues(under_test, {"short a", "a long value that spills over 1"}).has_value()
   );
   RHYDB_ASSERT(
      appendStringValues(under_test, {"short b", "a long value that spills over 2", "short c"})
         .has_value()
   );
   RHYDB_ASSERT(appendStringValues(under_test, {"a long value that spills over 3"}).has_value());

   EXPECT_EQ(under_test.numChunks(), 3);
   EXPECT_EQ(under_test.chunkSize(0), 2);
   EXPECT_EQ(under_test.chunkSize(1), 3);
   EXPECT_EQ(under_test.chunkSize(2), 1);
   EXPECT_EQ(under_test.getValueString(RowId(0, 0)), "short a");
   EXPECT_EQ(under_test.getValueString(RowId(0, 1)), "a long value that spills over 1");
   EXPECT_EQ(under_test.getValueString(RowId(1, 0)), "short b");
   EXPECT_EQ(under_test.getValueString(RowId(1, 1)), "a long value that spills over 2");
   EXPECT_EQ(under_test.getValueString(RowId(1, 2)), "short c");
   EXPECT_EQ(under_test.getValueString(RowId(2, 0)), "a long value that spills over 3");
}

TEST(StringColumn, manyLongValues) {
   std::vector<std::string> test_values;
   test_values.reserve(50000);
   for (size_t i = 0; i < 50000; ++i) {
      test_values.push_back(fmt::format("SOME_{}_LONG_{}_STRING", i, i));
   }

   StringColumnMetadata under_test("string_column");
   StringColumn column{&under_test};

   StringColumn::Builder builder;
   for (auto& value : test_values) {
      builder.insert(value);
   }
   RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());

   for (size_t i = 0; i < 50000; ++i) {
      ASSERT_EQ(column.getValue(RowId(0, i)).fastCompare(test_values.at(i)), std::nullopt);
      ASSERT_EQ(column.getValueString(RowId(0, i)), test_values.at(i));
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(StringColumn, manyMixedValues) {
   std::vector<std::string> test_values;
   test_values.reserve(50001);
   for (size_t i = 0; i < 50001; ++i) {
      if (i % 2 == 1) {
         test_values.emplace_back("SHRT");
      } else if (i % 10000 == 0) {
         test_values.push_back(fmt::format("{}_VERY_VERY_LONG_STRING_{}", i, std::string(i, 'x')));
      } else {
         test_values.push_back(fmt::format("{}_LONG_STRING_{}", i, std::string(100, 'x')));
      }
   }

   StringColumnMetadata under_test("string_column");
   StringColumn column{&under_test};

   StringColumn::Builder builder;
   for (auto& value : test_values) {
      builder.insert(value);
   }
   RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());

   for (size_t i = 0; i < 50001; ++i) {
      if (i % 2 == 1) {
         ASSERT_TRUE(column.getValue(RowId(0, i)).fastCompare(test_values.at(i)).has_value());
         ASSERT_EQ(
            column.getValue(RowId(0, i)).fastCompare(test_values.at(i)).value(),
            std::strong_ordering::equal
         );
         ASSERT_EQ(column.getValueString(RowId(0, i)), test_values.at(i));
      } else {
         ASSERT_EQ(column.getValue(RowId(0, i)).fastCompare(test_values.at(i)), std::nullopt);
         ASSERT_EQ(column.getValueString(RowId(0, i)), test_values.at(i));
      }
   }
}
