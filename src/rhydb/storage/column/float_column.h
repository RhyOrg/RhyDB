#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/chunked_value_buffer.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"

namespace rhydb::storage::column {

class FloatColumnBuilder;

class FloatColumn {
  public:
   using Metadata = ColumnMetadata;
   using Builder = FloatColumnBuilder;
   using Buffer = std::vector<std::optional<double>>;

   static constexpr const schema::DataType& TYPE = schema::DataType::FLOAT;
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::FLOAT;
   static schema::DataType type() { return TYPE; }
   using value_type = double;

  private:
   ChunkedValueBuffer<double> values;

  public:
   /// One container per chunk, indexed by chunk id: the rows of that chunk which are null.
   std::vector<roaring_util::RoaringContainer> null_bitmap;

   [[maybe_unused]] Metadata* metadata;

   explicit FloatColumn(ColumnMetadata* metadata);

   [[nodiscard]] size_t numChunks() const { return values.numChunks(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const { return values.chunkSize(chunk_id); }

   [[nodiscard]] bool isNull(RowId row_id) const {
      return null_bitmap[row_id.chunk_id].contains(row_id.row_in_chunk);
   }

   [[nodiscard]] double getValue(RowId row_id) const { return values.at(row_id); }

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   /// Assigns `value` to every row in `row_ids` (physical global row ids). A `std::nullopt` value
   /// marks the rows null; a concrete value clears their null flag and overwrites the stored value
   /// in place. Rows not in `row_ids` are left untouched.
   void update(const roaring::Roaring& row_ids, std::optional<double> value);

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
      archive & values;
      archive & null_bitmap;
      // clang-format on
   }
};

class FloatColumnBuilder {
   FloatColumn::Buffer buffer;

  public:
   void insert(double value) { buffer.emplace_back(value); }

   void insertNull() { buffer.emplace_back(std::nullopt); }

   void moveRowTo(size_t index, FloatColumnBuilder& destination) {
      destination.buffer.push_back(buffer.at(index));
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] FloatColumn::Buffer finalize() {
      FloatColumn::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

}  // namespace rhydb::storage::column
