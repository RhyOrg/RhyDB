#include "rhydb/storage/column/binary_column.h"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/row_layout.h"

using rhydb::storage::column::BinaryColumn;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::RowId;

namespace {

using Values = std::vector<std::optional<std::string>>;

/// Bytes that are not valid UTF-8 and contain a NUL byte
const std::string ARBITRARY_BYTES{"\x00\xff\xfe\x80 bytes", 10};

void appendChunk(BinaryColumn& column, const Values& values) {
   BinaryColumn::Builder builder;
   for (const auto& value : values) {
      if (value.has_value()) {
         builder.insert(*value);
      } else {
         builder.insertNull();
      }
   }
   ASSERT_TRUE(column.appendChunk(builder.finalize()).has_value());
}

std::optional<std::string> valueAt(const BinaryColumn& column, RowId row_id) {
   if (column.isNull(row_id)) {
      return std::nullopt;
   }
   return std::string{column.getValue(row_id)};
}

}  // namespace

TEST(BinaryColumn, storesArbitraryBytesAcrossChunks) {
   ColumnMetadata metadata{"binary_column"};
   BinaryColumn under_test(&metadata);

   const std::vector<Values> chunks{
      {ARBITRARY_BYTES, std::nullopt},
      {"", "plain", std::nullopt},
   };
   for (const auto& chunk : chunks) {
      appendChunk(under_test, chunk);
   }

   ASSERT_EQ(under_test.numChunks(), chunks.size());
   for (uint16_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      const auto& chunk = chunks.at(chunk_id);
      ASSERT_EQ(under_test.chunkSize(chunk_id), chunk.size());
      for (uint16_t row = 0; row < chunk.size(); ++row) {
         EXPECT_EQ(valueAt(under_test, RowId(chunk_id, row)), chunk.at(row));
      }
   }
   // An empty value is not null
   EXPECT_FALSE(under_test.isNull(RowId(1, 0)));
}

TEST(BinaryColumn, updateAssignsValuesAndNulls) {
   ColumnMetadata metadata{"binary_column"};
   BinaryColumn under_test(&metadata);
   appendChunk(under_test, {"a", "b"});
   appendChunk(under_test, {std::nullopt, "c", "d"});

   const roaring::Roaring to_update{RowId(0, 1).toGlobal(), RowId(1, 0).toGlobal()};
   under_test.update(to_update, ARBITRARY_BYTES);
   EXPECT_EQ(valueAt(under_test, RowId(0, 0)), "a");
   EXPECT_EQ(valueAt(under_test, RowId(0, 1)), ARBITRARY_BYTES);
   EXPECT_EQ(valueAt(under_test, RowId(1, 0)), ARBITRARY_BYTES);

   const roaring::Roaring rows{RowId(1, 1).toGlobal(), RowId(1, 2).toGlobal()};
   for (const auto& [chunk_id, chunk_rows] : rhydb::Bitmap{&rows}) {
      under_test.update(chunk_id, chunk_rows, Values{std::nullopt, "e"});
   }
   EXPECT_EQ(valueAt(under_test, RowId(1, 1)), std::nullopt);
   EXPECT_EQ(valueAt(under_test, RowId(1, 2)), "e");
}

TEST(BinaryColumn, deleteRowsKeepsTheRemainingValues) {
   ColumnMetadata metadata{"binary_column"};
   BinaryColumn under_test(&metadata);
   appendChunk(under_test, {"a", std::nullopt, "c"});
   appendChunk(under_test, {"d"});

   const roaring::Roaring deleted{RowId(0, 0).toGlobal(), RowId(1, 0).toGlobal()};
   const rhydb::storage::column::RowCompaction compaction{
      rhydb::storage::column::RowLayout::of(3, 1), rhydb::Bitmap{&deleted}
   };
   under_test.deleteRows(compaction);

   ASSERT_EQ(under_test.numChunks(), 1);
   ASSERT_EQ(under_test.chunkSize(0), 2);
   EXPECT_EQ(valueAt(under_test, RowId(0, 0)), std::nullopt);
   EXPECT_EQ(valueAt(under_test, RowId(0, 1)), "c");
}

TEST(BinaryColumn, survivesSerialization) {
   ColumnMetadata metadata{"binary_column"};
   BinaryColumn original(&metadata);
   appendChunk(original, {ARBITRARY_BYTES, std::nullopt});

   std::stringstream buffer;
   {
      boost::archive::binary_oarchive output{buffer};
      output << original;
   }
   BinaryColumn loaded(&metadata);
   boost::archive::binary_iarchive input{buffer};
   input >> loaded;

   EXPECT_EQ(valueAt(loaded, RowId(0, 0)), ARBITRARY_BYTES);
   EXPECT_EQ(valueAt(loaded, RowId(0, 1)), std::nullopt);
}
