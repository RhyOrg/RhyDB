#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

class BoolColumnBuilder;

/// The rows of one chunk of a `BoolColumn`, as rows within the chunk. Every row of the chunk is in
/// exactly one of the three containers.
struct BoolColumnChunk {
   roaring_util::RoaringContainer true_rows = roaring_util::RoaringContainer::withCapacity(0);
   roaring_util::RoaringContainer false_rows = roaring_util::RoaringContainer::withCapacity(0);
   roaring_util::RoaringContainer null_rows = roaring_util::RoaringContainer::withCapacity(0);

   [[nodiscard]] uint32_t size() const {
      return true_rows.getCardinality() + false_rows.getCardinality() + null_rows.getCardinality();
   }

   [[nodiscard]] bool isNull(uint16_t row_in_chunk) const {
      return null_rows.contains(row_in_chunk);
   }

   [[nodiscard]] bool getValue(uint16_t row_in_chunk) const {
      RHYDB_ASSERT(!isNull(row_in_chunk));
      return true_rows.contains(row_in_chunk);
   }

   /// Assigns `value` to the row `row_in_chunk`; `std::nullopt` marks it null.
   void setValue(uint16_t row_in_chunk, std::optional<bool> value);

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & true_rows;
      archive & false_rows;
      archive & null_rows;
      // clang-format on
   }
};

class BoolColumn {
  public:
   using Metadata = ColumnMetadata;
   using Builder = BoolColumnBuilder;
   using Buffer = std::vector<std::optional<bool>>;

   static constexpr const schema::DataType& TYPE = schema::DataType::BOOL;
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::BOOL;
   static schema::DataType type() { return TYPE; }
   using value_type = bool;

   /// One entry per chunk, indexed by chunk id.
   std::vector<BoolColumnChunk> chunks;

   Metadata* metadata;

   explicit BoolColumn(Metadata* metadata);

   [[nodiscard]] size_t numChunks() const { return chunks.size(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const { return chunks.at(chunk_id).size(); }

   [[nodiscard]] bool getValue(RowId row_id) const {
      return chunks[row_id.chunk_id].getValue(row_id.row_in_chunk);
   }

   [[nodiscard]] bool isNull(RowId row_id) const {
      return chunks[row_id.chunk_id].isNull(row_id.row_in_chunk);
   }

   /// The rows that are true, viewing into this column.
   [[nodiscard]] Bitmap trueRows() const;

   /// The rows that are false, viewing into this column.
   [[nodiscard]] Bitmap falseRows() const;

   /// The rows that are null, viewing into this column.
   [[nodiscard]] Bitmap nullRows() const;

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   /// Assigns `value` to every row in `row_ids` (physical global row ids); `std::nullopt` marks
   /// them null. Rows not in `row_ids` are left untouched.
   void update(const roaring::Roaring& row_ids, std::optional<bool> value);

   /// Assigns `new_values[i]` to the `i`-th row (in ascending order) of `rows` within the chunk
   /// `chunk_id`; a `std::nullopt` value marks the row null. Rows not in `rows` are left untouched.
   void update(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows,
      const Buffer& new_values
   );

   /// Removes the rows `compaction` deletes and renumbers the remaining ones, see `RowCompaction`.
   void deleteRows(const RowCompaction& compaction);

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & chunks;
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
