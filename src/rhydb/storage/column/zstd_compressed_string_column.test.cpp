#include "rhydb/storage/column/zstd_compressed_string_column.h"

#include <optional>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/storage/column/row_id.h"

using rhydb::storage::column::RowId;

TEST(ZstdCompressedStringColumn, insertValuesAndGetThemBack) {
   rhydb::storage::column::ZstdCompressedStringColumnMetadata column_metadata{
      "test_column", "ACGT"
   };
   rhydb::storage::column::ZstdCompressedStringColumn under_test(&column_metadata);

   std::vector<std::optional<std::string>> values_to_add{
      "2020-01-01", "2023-01-05", "2021-12-03", "2025-01-01", std::nullopt, "2021-03-21", "asd"
   };
   rhydb::storage::column::ZstdCompressedStringColumn::Builder builder;
   for (const auto& value : values_to_add) {
      if (value.has_value()) {
         builder.insert(value.value());
      } else {
         builder.insertNull();
      }
   }
   RHYDB_ASSERT(under_test.appendChunk(builder.finalize()).has_value());

   ASSERT_EQ(under_test.numChunks(), 1);
   ASSERT_EQ(under_test.chunkSize(0), values_to_add.size());

   for (size_t value_idx = 0; value_idx < values_to_add.size(); ++value_idx) {
      ASSERT_EQ(under_test.getDecompressed(RowId(0, value_idx)), values_to_add.at(value_idx));
   }
}

namespace {
void appendChunk(
   rhydb::storage::column::ZstdCompressedStringColumn& column,
   const std::vector<std::optional<std::string>>& values
) {
   rhydb::storage::column::ZstdCompressedStringColumn::Builder builder;
   for (const auto& value : values) {
      if (value.has_value()) {
         builder.insert(value.value());
      } else {
         builder.insertNull();
      }
   }
   RHYDB_ASSERT(column.appendChunk(builder.finalize()).has_value());
}
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ZstdCompressedStringColumn, updateOverwritesCompressedValuesAcrossChunks) {
   rhydb::storage::column::ZstdCompressedStringColumnMetadata column_metadata{
      "test_column", "ACGT"
   };
   rhydb::storage::column::ZstdCompressedStringColumn under_test(&column_metadata);

   appendChunk(under_test, {"2020-01-01", "2023-01-05"});
   appendChunk(under_test, {std::nullopt, "2021-12-03"});

   // Overwrite a concrete row and a previously-null row spanning both chunks with the same value.
   roaring::Roaring to_update;
   to_update.add(RowId(0, 1).toGlobal());
   to_update.add(RowId(1, 0).toGlobal());
   under_test.update(to_update, "2099-09-09");

   EXPECT_EQ(under_test.getDecompressed(RowId(0, 0)), "2020-01-01");  // untouched
   EXPECT_EQ(under_test.getDecompressed(RowId(0, 1)), "2099-09-09");
   EXPECT_EQ(under_test.getDecompressed(RowId(1, 0)), "2099-09-09");
   EXPECT_FALSE(under_test.isNull(RowId(1, 0)));
   EXPECT_EQ(under_test.getDecompressed(RowId(1, 1)), "2021-12-03");  // untouched

   // A nullopt update marks the row null again.
   roaring::Roaring to_null;
   to_null.add(RowId(0, 1).toGlobal());
   under_test.update(to_null, std::nullopt);
   EXPECT_TRUE(under_test.isNull(RowId(0, 1)));
   EXPECT_EQ(under_test.getDecompressed(RowId(0, 1)), std::nullopt);
}

// Each appendChunk starts a fresh, immutable chunk whose global row ids begin at a fresh
// 2^16-aligned offset (chunk k starts at k << 16); the null bitmap stores those aligned row ids.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ZstdCompressedStringColumn, valuesSpanningMultipleAppendedChunks) {
   using rhydb::storage::column::RowId;
   rhydb::storage::column::ZstdCompressedStringColumnMetadata column_metadata{
      "test_column", "ACGT"
   };
   rhydb::storage::column::ZstdCompressedStringColumn under_test(&column_metadata);

   const std::vector<std::vector<std::optional<std::string>>> chunks{
      {"2020-01-01", "2023-01-05"},
      {std::nullopt, "2021-12-03", "2025-01-01", std::nullopt},
      {"asd"},
   };
   const auto num_chunks = static_cast<uint16_t>(chunks.size());
   for (const auto& chunk : chunks) {
      appendChunk(under_test, chunk);
   }

   ASSERT_EQ(under_test.numChunks(), chunks.size());

   for (uint16_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
      ASSERT_EQ(under_test.chunkSize(chunk_id), chunks.at(chunk_id).size());
   }

   for (uint16_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
      const auto& chunk = chunks.at(chunk_id);
      for (size_t row_in_chunk = 0; row_in_chunk < chunk.size(); ++row_in_chunk) {
         const auto row_id = RowId(chunk_id, static_cast<uint16_t>(row_in_chunk));
         ASSERT_EQ(under_test.getDecompressed(row_id), chunk.at(row_in_chunk));
         ASSERT_EQ(under_test.isNull(row_id), !chunk.at(row_in_chunk).has_value());
      }
   }
}
