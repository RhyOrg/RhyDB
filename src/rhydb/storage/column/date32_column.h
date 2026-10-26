#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/date32.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/chunked_value_buffer.h"
#include "rhydb/storage/column/column_metadata.h"

namespace rhydb::storage::column {

class Date32ColumnBuilder;

class Date32Column {
  public:
   using Metadata = ColumnMetadata;
   using Builder = Date32ColumnBuilder;
   /// Dates are parsed during phase-1 extraction so the chunk holds parsed values.
   using Buffer = std::vector<std::optional<common::Date32>>;

   static constexpr const schema::DataType& TYPE = schema::DataType::DATE32;
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::DATE32;
   static schema::DataType type() { return TYPE; }
   using value_type = common::Date32;

   [[maybe_unused]] Metadata* metadata;
   /// One container per chunk, indexed by chunk id: the rows of that chunk which are null.
   std::vector<roaring_util::RoaringContainer> null_bitmap;

  private:
   ChunkedValueBuffer<common::Date32> values;
   bool is_sorted = true;
   /// Transient ingestion state: the last non-null value appended, used to detect whether values
   /// stay sorted across chunk boundaries. Not serialized.
   std::optional<common::Date32> last_appended_value;

  public:
   explicit Date32Column(Metadata* metadata);

   [[nodiscard]] bool isSorted() const;

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   /// Assigns `value` to every row in `row_ids` (physical global row ids). A `std::nullopt` value
   /// marks the rows null; a concrete value clears their null flag and overwrites the stored value
   /// in place. Rows not in `row_ids` are left untouched. Since an update can move a value in
   /// either direction, the column's sortedness is recomputed by a full scan afterwards.
   void update(const roaring::Roaring& row_ids, std::optional<common::Date32> value);

   /// Assigns `new_values[i]` to the `i`-th row (in ascending order) of `rows` within the chunk
   /// `chunk_id`; a `std::nullopt` value marks the row null. Rows not in `rows` are left untouched.
   /// The column stays sorted if it was and the chunk is still sorted against its neighbours; an
   /// unsorted column is not checked for having become sorted again.
   void update(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows,
      const Buffer& new_values
   );

   /// The per-chunk value buffers. Used by `DateBetween` to binary search a sorted column.
   [[nodiscard]] const ChunkedValueBuffer<common::Date32>& getValueBuffer() const { return values; }

   [[nodiscard]] size_t numChunks() const { return values.numChunks(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const { return values.chunkSize(chunk_id); }

   [[nodiscard]] bool isNull(RowId row_id) const {
      return null_bitmap[row_id.chunk_id].contains(row_id.row_in_chunk);
   }

   [[nodiscard]] common::Date32 getValue(RowId row_id) const { return values.at(row_id); }

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & null_bitmap;
      archive & values;
      archive & is_sorted;
      if constexpr (Archive::is_loading::value) {
         if (values.numChunks() > 0) {
            last_appended_value = values.lastValue();
         }
      }
      // clang-format on
   }
};

class Date32ColumnBuilder {
   Date32Column::Buffer buffer;

  public:
   void insert(common::Date32 value) { buffer.emplace_back(value); }

   void insertNull() { buffer.emplace_back(std::nullopt); }

   void moveRowTo(size_t index, Date32ColumnBuilder& destination) {
      destination.buffer.push_back(buffer.at(index));
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] Date32Column::Buffer finalize() {
      Date32Column::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

}  // namespace rhydb::storage::column
