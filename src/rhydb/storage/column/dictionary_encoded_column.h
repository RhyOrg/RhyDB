#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/assume_abstract.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/types.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/roaring_util/roaring_serialize.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/chunked_value_buffer.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/string_column.h"

namespace rhydb::storage::column {

class DictionaryEncodedColumnBuilder;

class DictionaryEncodedColumn {
  public:
   using Metadata = ColumnMetadata;
   using Builder = DictionaryEncodedColumnBuilder;
   using Buffer = std::vector<std::optional<std::string>>;

   static inline const schema::DataType TYPE =
      schema::DataType::dictionaryEncoded(schema::DataType::STRING);
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::DICTIONARY_ENCODED;
   static schema::DataType type() { return TYPE; }
   using value_type = std::string_view;

   Metadata* metadata;
   roaring::Roaring null_bitmap;

  private:
   /// Owned by this column, because `insertDictionaryValues` renumbers the ids that only this
   /// column's `value_ids` and `indexed_values` refer to. Behind a pointer so that `dictionary`
   /// keeps pointing to it when the column is moved.
   std::unique_ptr<StringColumn::Metadata> dictionary_metadata;
   /// The dictionary: every distinct value of the column exactly once, in ascending (byte-wise)
   /// order. A value's dictionary id is its row id in this sub-column, so ids are order-preserving.
   StringColumn dictionary;
   ChunkedValueBuffer<Idx> value_ids;
   std::unordered_map<Idx, roaring::Roaring> indexed_values;

  public:
   explicit DictionaryEncodedColumn(Metadata* metadata);

   [[nodiscard]] std::optional<const roaring::Roaring*> filter(rhydb::Idx value_id) const;

   [[nodiscard]] std::optional<const roaring::Roaring*> filter(
      const std::optional<std::string>& value
   ) const;

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   void update(const roaring::Roaring& row_ids, const std::optional<std::string>& value);

   /// Assigns `new_values[i]` to the `i`-th row (in ascending order) of `rows` within the chunk
   /// `chunk_id`; a `std::nullopt` value marks the row null. Rows not in `rows` are left untouched.
   void update(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows,
      const Buffer& new_values
   );

   [[nodiscard]] size_t numChunks() const { return value_ids.numChunks(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const {
      return value_ids.chunkSize(chunk_id);
   }

   [[nodiscard]] const Idx& getValue(RowId row_id) const { return value_ids.at(row_id); }

   /// The inverted index: for every distinct dictionary id that occurs, the rows carrying it. Null
   /// rows are excluded (they live in `null_bitmap`), so these bitmaps are disjoint from it. Used
   /// by the bitmap-aggregation node to group by this column straight from the index.
   [[nodiscard]] const std::unordered_map<Idx, roaring::Roaring>& getIndexedValues() const {
      return indexed_values;
   }

   [[nodiscard]] bool isNull(RowId row_id) const;

   [[nodiscard]] std::string getValueString(RowId row_id) const {
      return lookupValue(getValue(row_id));
   }

   [[nodiscard]] std::string lookupValue(Idx dict_id) const;

   [[nodiscard]] size_t numDictionaryValues() const;

   [[nodiscard]] std::optional<rhydb::Idx> getValueId(const std::string& value) const;

  private:
   /// Binary searches the sorted dictionary for `value`.
   [[nodiscard]] std::optional<Idx> findDictionaryId(std::string_view value) const;

   /// Merges `new_values` (sorted, distinct and none of them in the dictionary yet) into the
   /// dictionary. Every value sorting after an inserted one moves to a higher id, so the returned
   /// vector maps each old id to its new id.
   [[nodiscard]] std::vector<Idx> insertDictionaryValues(
      const std::vector<std::string_view>& new_values
   );

   /// Returns the dictionary id of every distinct value in `buffer` (a null maps to the empty
   /// string placeholder), inserting the missing ones into the dictionary first. The returned keys
   /// view into `buffer`. Inserting renumbers existing ids, so the stored ids and the inverted
   /// index are remapped before this returns.
   std::unordered_map<std::string_view, Idx> internValues(const Buffer& buffer);

   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & dictionary;
      archive & value_ids;
      archive & indexed_values;
      archive & null_bitmap;
      // clang-format on
   }
};

class DictionaryEncodedColumnBuilder {
   DictionaryEncodedColumn::Buffer buffer;

  public:
   void insert(std::string_view value) { buffer.emplace_back(std::string{value}); }

   void insertNull() { buffer.emplace_back(std::nullopt); }

   void moveRowTo(size_t index, DictionaryEncodedColumnBuilder& destination) {
      destination.buffer.push_back(std::move(buffer.at(index)));
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] DictionaryEncodedColumn::Buffer finalize() {
      DictionaryEncodedColumn::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

}  // namespace rhydb::storage::column
