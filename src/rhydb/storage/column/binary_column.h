#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/data_type.h"
#include "rhydb/storage/column/chunked_value_buffer.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

class BinaryColumnBuilder;

/// A column of arbitrary byte strings, stored as given. It has no index: queries read its values
/// as Arrow `large_binary` arrays and evaluate them with Arrow compute functions.
class BinaryColumn {
  public:
   using Metadata = ColumnMetadata;
   using Builder = BinaryColumnBuilder;
   using Buffer = std::vector<std::optional<std::string>>;

   static constexpr const schema::DataTypePtr& TYPE = schema::DataType::BINARY;
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::BINARY;
   static const schema::DataTypePtr& type() { return TYPE; }
   using value_type = std::string_view;

  private:
   /// The value of every row; a null row holds an empty placeholder
   ChunkedValueBuffer<std::string> values;

  public:
   /// One container per chunk, indexed by chunk id: the rows of that chunk which are null.
   std::vector<roaring_util::RoaringContainer> null_bitmap;
   Metadata* metadata;

   explicit BinaryColumn(Metadata* metadata);

   [[nodiscard]] std::expected<void, std::string> appendChunk(const Buffer& buffer);

   void update(const roaring::Roaring& row_ids, const std::optional<std::string>& value);

   /// Assigns `new_values[i]` to the `i`-th row (in ascending order) of `rows` within the chunk
   /// `chunk_id`; a `std::nullopt` value marks the row null. Rows not in `rows` are left untouched.
   void update(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows,
      const Buffer& new_values
   );

   /// Removes the rows `compaction` deletes and renumbers the remaining ones, see `RowCompaction`.
   void deleteRows(const RowCompaction& compaction);

   [[nodiscard]] bool isNull(RowId row_id) const;

   /// The bytes of the non-null row `row_id`
   [[nodiscard]] std::string_view getValue(RowId row_id) const { return values.at(row_id); }

   [[nodiscard]] size_t numChunks() const { return values.numChunks(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const { return values.chunkSize(chunk_id); }

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

class BinaryColumnBuilder {
   BinaryColumn::Buffer buffer;

  public:
   void insert(std::string_view value) { buffer.emplace_back(std::string{value}); }

   void insertNull() { buffer.emplace_back(std::nullopt); }

   void moveRowTo(size_t index, BinaryColumnBuilder& destination) {
      destination.buffer.push_back(std::move(buffer.at(index)));
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] BinaryColumn::Buffer finalize() {
      BinaryColumn::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

}  // namespace rhydb::storage::column
