#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/german_string.h"
#include "rhydb/common/types.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/roaring_util/roaring_serialize.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/chunked_value_buffer.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/string_column.h"

namespace rhydb::storage::column {

/// A column type that can hold the dictionary of a `DictionaryEncodedColumn`: one for each value
/// type that `schema::DataType::canBeDictionaryEncoded`
template <typename ValueColumn>
concept DictionaryValueColumn = std::same_as<ValueColumn, StringColumn>;

/// How the values of a `ValueColumn` are held, viewed and ordered inside a dictionary.
template <DictionaryValueColumn ValueColumn>
struct DictionaryValues {
   /// An owned value, as buffered for ingestion
   using Value = ValueColumn::Buffer::value_type::value_type;
   /// A non-owning view of a value, used for lookups
   using Key = std::conditional_t<std::is_same_v<Value, std::string>, std::string_view, Value>;

   static constexpr bool IS_STRING = std::is_same_v<Value, std::string>;

   /// The order of the dictionary: byte-wise for strings
   static std::strong_ordering compare(Key lhs, Key rhs) {
      if constexpr (IS_STRING) {
         return compareBytesUnsigned(lhs, rhs);
      } else {
         return lhs <=> rhs;
      }
   }

   static Value read(const ValueColumn& column, RowId row_id) {
      if constexpr (IS_STRING) {
         return column.getValueString(row_id);
      } else {
         return column.getValue(row_id);
      }
   }

   static std::strong_ordering compareAt(const ValueColumn& column, RowId row_id, Key key) {
      if constexpr (IS_STRING) {
         if (auto ordering = column.getValue(row_id).fastCompare(key)) {
            return *ordering;
         }
      }
      return compare(read(column, row_id), key);
   }

   using KeyMap = std::unordered_map<Key, Idx>;
};

template <DictionaryValueColumn ValueColumn>
class DictionaryEncodedColumnBuilder;

/// A column storing for every row the id of its value in a sorted dictionary of the distinct
/// values (held as a `ValueColumn`), plus an inverted index from each id to the rows carrying it.
template <DictionaryValueColumn ValueColumn>
class DictionaryEncodedColumn {
   using Values = DictionaryValues<ValueColumn>;

  public:
   using Metadata = ColumnMetadata;
   using Builder = DictionaryEncodedColumnBuilder<ValueColumn>;
   using Value = Values::Value;
   using Buffer = std::vector<std::optional<Value>>;

   static inline const schema::DataType TYPE =
      schema::DataType::dictionaryEncoded(ValueColumn::TYPE);
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::DICTIONARY_ENCODED;
   static schema::DataType type() { return TYPE; }
   using value_type = ValueColumn::value_type;

   Metadata* metadata;
   roaring::Roaring null_bitmap;

  private:
   /// Owned by this column, because `insertDictionaryValues` renumbers the ids that only this
   /// column's `value_ids` and `indexed_values` refer to. Behind a pointer so that `dictionary`
   /// keeps pointing to it when the column is moved.
   std::unique_ptr<typename ValueColumn::Metadata> dictionary_metadata;
   /// The dictionary: every distinct value of the column exactly once, in ascending order (see
   /// `DictionaryValues::compare`). A value's dictionary id is its row id in this sub-column, so
   /// ids are order-preserving.
   ValueColumn dictionary;
   ChunkedValueBuffer<Idx> value_ids;
   std::unordered_map<Idx, roaring::Roaring> indexed_values;

  public:
   explicit DictionaryEncodedColumn(Metadata* metadata);

   /// Not an overload of `filter`, which would otherwise be chosen for integer values
   [[nodiscard]] std::optional<const roaring::Roaring*> filterById(rhydb::Idx value_id) const;

   [[nodiscard]] std::optional<const roaring::Roaring*> filter(
      const std::optional<Value>& value
   ) const;

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   void update(const roaring::Roaring& row_ids, const std::optional<Value>& value);

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

   [[nodiscard]] std::string getValueString(RowId row_id) const
      requires(Values::IS_STRING)
   {
      return lookupValue(getValue(row_id));
   }

   [[nodiscard]] Value lookupValue(Idx dict_id) const;

   [[nodiscard]] size_t numDictionaryValues() const;

   [[nodiscard]] std::optional<rhydb::Idx> getValueId(Values::Key value) const;

  private:
   /// Binary searches the sorted dictionary for `value`.
   [[nodiscard]] std::optional<Idx> findDictionaryId(Values::Key value) const;

   /// Merges `new_values` (sorted, distinct and none of them in the dictionary yet) into the
   /// dictionary. Every value sorting after an inserted one moves to a higher id, so the returned
   /// vector maps each old id to its new id.
   [[nodiscard]] std::vector<Idx> insertDictionaryValues(
      const std::vector<typename Values::Key>& new_values
   );

   /// Returns the dictionary id of every distinct value in `buffer` (a null maps to the
   /// default-constructed placeholder value), inserting the missing ones into the dictionary first.
   /// String keys view into `buffer`. Inserting renumbers existing ids, so the stored ids and the
   /// inverted index are remapped before this returns.
   Values::KeyMap internValues(const Buffer& buffer);

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

template <DictionaryValueColumn ValueColumn>
class DictionaryEncodedColumnBuilder {
   using EncodedColumn = DictionaryEncodedColumn<ValueColumn>;

   EncodedColumn::Buffer buffer;

  public:
   void insert(EncodedColumn::value_type value) {
      buffer.emplace_back(typename EncodedColumn::Value{value});
   }

   void insertNull() { buffer.emplace_back(std::nullopt); }

   void moveRowTo(size_t index, DictionaryEncodedColumnBuilder& destination) {
      destination.buffer.push_back(std::move(buffer.at(index)));
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] EncodedColumn::Buffer finalize() {
      typename EncodedColumn::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

extern template class DictionaryEncodedColumn<StringColumn>;

}  // namespace rhydb::storage::column
