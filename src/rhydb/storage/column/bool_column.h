#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

class BoolColumnBuilder;

class BoolColumn {
  public:
   using Metadata = ColumnMetadata;
   using Builder = BoolColumnBuilder;
   using Buffer = std::vector<std::optional<bool>>;

   static constexpr const schema::DataType& TYPE = schema::DataType::BOOL;
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::BOOL;
   static schema::DataType type() { return TYPE; }
   using value_type = bool;

   roaring::Roaring true_bitmap;
   roaring::Roaring false_bitmap;
   /// One container per chunk, indexed by chunk id: the rows of that chunk which are null.
   std::vector<roaring_util::RoaringContainer> null_bitmap;

   Metadata* metadata;

  private:
   /// Number of appended chunks. The bitmaps are keyed by global row id, and chunk `k` starts at
   /// `k << 16`, so this counter is needed to place each new chunk in its own 2^16 block.
   uint16_t num_chunks = 0;

  public:
   explicit BoolColumn(Metadata* metadata);

   [[nodiscard]] size_t numChunks() const { return num_chunks; }

   /// Number of rows in chunk `chunk_id`. Every row lands in exactly one of the three disjoint
   /// bitmaps at its sparse global id, so the chunk's size is the combined cardinality within its
   /// 2^16-aligned block. Derived on demand (the column stores no per-chunk sizes), which is fine
   /// as this only exists to satisfy the `Column` concept and is not on any hot path.
   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const {
      const uint64_t base = RowId::chunkStart(chunk_id);
      const auto last = static_cast<uint32_t>(base + 0xFFFF);
      const auto count_in_block = [&](const roaring::Roaring& bitmap) -> uint64_t {
         const uint64_t up_to_last = bitmap.rank(last);
         const uint64_t before_base = base == 0 ? 0 : bitmap.rank(static_cast<uint32_t>(base - 1));
         return up_to_last - before_base;
      };
      return static_cast<uint32_t>(
         count_in_block(true_bitmap) + count_in_block(false_bitmap) +
         null_bitmap.at(chunk_id).getCardinality()
      );
   }

   [[nodiscard]] bool getValue(RowId row_id) const {
      RHYDB_ASSERT(!null_bitmap[row_id.chunk_id].contains(row_id.row_in_chunk));
      return true_bitmap.contains(row_id.toGlobal());
   }

   [[nodiscard]] bool isNull(RowId row_id) const {
      return null_bitmap[row_id.chunk_id].contains(row_id.row_in_chunk);
   }

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   /// Assigns `value` to every row in `row_ids` (physical global row ids). Each row is first
   /// removed from all three bitmaps and then re-classified into the true, false or null bitmap
   /// according to `value` (`std::nullopt` marks it null). Rows not in `row_ids` are left
   /// untouched.
   void update(const roaring::Roaring& row_ids, std::optional<bool> value);

   /// Assigns `new_values[i]` to the `i`-th row (in ascending order) of `rows` within the chunk
   /// `chunk_id`; a `std::nullopt` value marks the row null. Rows not in `rows` are left untouched.
   void update(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows,
      const Buffer& new_values
   );

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & true_bitmap;
      archive & false_bitmap;
      archive & null_bitmap;
      archive & num_chunks;
      // clang-format on
   }
};

class BoolColumnBuilder {
   BoolColumn::Buffer buffer;

  public:
   void insert(bool value) { buffer.emplace_back(value); }

   void insertNull() { buffer.emplace_back(std::nullopt); }

   void moveRowTo(size_t index, BoolColumnBuilder& destination) {
      destination.buffer.push_back(buffer.at(index));
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] BoolColumn::Buffer finalize() {
      BoolColumn::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

}  // namespace rhydb::storage::column
