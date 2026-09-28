#include "rhydb/query_engine/batched_bitmap_reader.h"

#include <gtest/gtest.h>

#include <roaring/roaring.hh>

using rhydb::query_engine::BatchedBitmapReader;

TEST(BatchedBitmapReader, batchesCorrectly) {
   roaring::Roaring bitmap{0, 1, 2, 3, 4};
   BatchedBitmapReader under_test{bitmap, 1};
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{0, 1}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{2, 3}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{4}));
   ASSERT_EQ(under_test.nextBatch(), std::nullopt);
}

TEST(BatchedBitmapReader, batchesCorrectlySingletons) {
   roaring::Roaring bitmap{0, 1, 3, 4};
   BatchedBitmapReader under_test{bitmap, 0};
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{0}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{1}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{3}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{4}));
   ASSERT_EQ(under_test.nextBatch(), std::nullopt);
}

TEST(BatchedBitmapReader, batchesCorrectlyEmpty) {
   roaring::Roaring bitmap{};
   BatchedBitmapReader under_test{bitmap, 22};
   ASSERT_EQ(under_test.nextBatch(), std::nullopt);
}

TEST(BatchedBitmapReader, batchesCorrectlyLargeValues) {
   uint32_t offset = 1 << 20;
   roaring::Roaring bitmap{
      offset + 1, offset + 3, offset + 5, offset + 7, offset + 9, offset + 11, offset + 13,
   };
   BatchedBitmapReader under_test{bitmap, 2};
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{offset + 1, offset + 3, offset + 5}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{offset + 7, offset + 9, offset + 11}));
   ASSERT_EQ(under_test.nextBatch(), (roaring::Roaring{offset + 13}));
   ASSERT_EQ(under_test.nextBatch(), std::nullopt);
}

TEST(BatchedBitmapReader, batchesCorrectlyHundredsOfValues) {
   const size_t batch_size = 75;

   roaring::Roaring initial_bitmap;
   for (uint32_t i = 0; i < 500; ++i) {
      initial_bitmap.add(i);
   }
   initial_bitmap.add(1000);
   initial_bitmap.add(1002);
   initial_bitmap.add(1004);

   BatchedBitmapReader under_test{initial_bitmap, batch_size - 1};

   size_t expected_batches = (initial_bitmap.cardinality() + batch_size - 1) / batch_size;
   size_t current_value_idx = 0;

   for (size_t i = 0; i < expected_batches; ++i) {
      std::optional<roaring::Roaring> actual_batch = under_test.nextBatch();
      ASSERT_TRUE(actual_batch.has_value());

      roaring::Roaring expected_batch;
      for (uint32_t j = 0; j < batch_size && current_value_idx < initial_bitmap.cardinality();
           ++j) {
         uint32_t val;
         initial_bitmap.select(current_value_idx, &val);
         expected_batch.add(val);
         current_value_idx++;
      }

      ASSERT_EQ(*actual_batch, expected_batch);
   }

   ASSERT_EQ(under_test.nextBatch(), std::nullopt);
}
