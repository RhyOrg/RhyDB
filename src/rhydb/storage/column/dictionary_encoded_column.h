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
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/german_string.h"
#include "rhydb/common/types.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/string_column.h"

namespace rhydb::storage::column {

/// A column type that can hold the dictionary of a `DictionaryEncodedColumn`: one for each value
/// type that `schema::DataType::canBeDictionaryEncoded`
template <typename ValueColumn>
concept DictionaryValueColumn =
   std::same_as<ValueColumn, StringColumn> || std::same_as<ValueColumn, Date32Column>;

/// How the values of a `ValueColumn` are held, viewed and ordered inside a dictionary.
template <DictionaryValueColumn ValueColumn>
struct DictionaryValues {
   /// A non-owning view of a value, used for lookups
   using Key = ValueColumn::value_type;

   static constexpr bool IS_STRING = std::is_same_v<Key, std::string_view>;

   /// The order of the dictionary: byte-wise for strings, chronological for dates
   static std::strong_ordering compare(Key lhs, Key rhs) {
      if constexpr (IS_STRING) {
         return compareBytesUnsigned(lhs, rhs);
      } else {
         return lhs <=> rhs;
      }
   }

   /// An owned copy of the value at `row_id`, as strings cannot be viewed in place
   static auto read(const ValueColumn& column, RowId row_id) {
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

/// The rows of one chunk of a `DictionaryEncodedColumn`, as rows within the chunk.
struct DictionaryEncodedColumnChunk {
   /// The dictionary id of the value of every row. A null row holds the id of the
   /// default-constructed placeholder value, and is in `null_rows` instead of `rows_by_value_id`.
   std::vector<Idx> value_ids;
   roaring_util::RoaringContainer null_rows = roaring_util::RoaringContainer::withCapacity(0);
   /// The inverted index: for every dictionary id that a non-null row holds, those rows. Holds no
   /// empty containers.
   std::unordered_map<Idx, roaring_util::RoaringContainer> rows_by_value_id;

   [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(value_ids.size()); }

   [[nodiscard]] bool isNull(uint16_t row_in_chunk) const {
      return null_rows.contains(row_in_chunk);
   }

   /// Assigns the value with id `value_id` to the row `row_in_chunk`, or makes it null with the
   /// placeholder id `value_id` if `is_null`.
   void setValueId(uint16_t row_in_chunk, Idx value_id, bool is_null);

   /// Adds the row `row_in_chunk` to the rows of `value_id` in the inverted index.
   void addToRowsByValueId(uint16_t row_in_chunk, Idx value_id);

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & value_ids;
      archive & null_rows;
      archive & rows_by_value_id;
      // clang-format on
   }
};

/// A column storing for every row the id of its value in a sorted dictionary of the distinct
/// values (held as a `ValueColumn`), plus an inverted index from each id to the rows carrying it.
template <DictionaryValueColumn ValueColumn>
class DictionaryEncodedColumn {
   using Values = DictionaryValues<ValueColumn>;

  public:
   using Metadata = ColumnMetadata;
   /// Ingestion buffers the values just like the value column does; they are dictionary-encoded
   /// when a chunk is appended
   using Builder = ValueColumn::Builder;
   using Buffer = ValueColumn::Buffer;
   using Key = Values::Key;

   static inline const schema::DataType TYPE =
      schema::DataType::dictionaryEncoded(ValueColumn::TYPE);
   static constexpr schema::DataType::Kind KIND = schema::DataType::Kind::DICTIONARY_ENCODED;
   static schema::DataType type() { return TYPE; }
   using value_type = ValueColumn::value_type;

   Metadata* metadata;

  private:
   /// Owned by this column, because `insertDictionaryValues` renumbers the ids that only this
   /// column's chunks refer to. Behind a pointer so that `dictionary` keeps pointing to it when the
   /// column is moved.
   std::unique_ptr<typename ValueColumn::Metadata> dictionary_metadata;
   /// The dictionary: every distinct value of the column exactly once, in ascending order (see
   /// `DictionaryValues::compare`). A value's dictionary id is its row id in this sub-column, so
   /// ids are order-preserving.
   ValueColumn dictionary;
   /// One entry per chunk, indexed by chunk id.
   std::vector<DictionaryEncodedColumnChunk> chunks;

  public:
   explicit DictionaryEncodedColumn(Metadata* metadata);

   /// The rows holding the value with id `value_id`, viewing into this column. Not an overload of
   /// `filter`, which would otherwise be chosen for integer values
   [[nodiscard]] Bitmap filterById(rhydb::Idx value_id) const;

   /// The rows holding `value`, viewing into this column, or nullopt if the column has never held
   /// it. Null rows never hold a value, they are in `nullRows`.
   [[nodiscard]] std::optional<Bitmap> filter(Key value) const;

   /// The rows holding a value whose id satisfies `matches_id`, which is called once per
   /// dictionary id. Null rows never hold a value.
   template <std::predicate<Idx> MatchesId>
   [[nodiscard]] Bitmap filterByIds(const MatchesId& matches_id) const {
      std::vector<bool> matching_ids(numDictionaryValues());
      for (size_t value_id = 0; value_id < matching_ids.size(); ++value_id) {
         matching_ids[value_id] = matches_id(static_cast<Idx>(value_id));
      }
      return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
         std::vector<roaring_util::RoaringContainerView> matching_rows;
         for (const auto& [value_id, rows] : chunks[chunk_id].rows_by_value_id) {
            if (matching_ids[value_id]) {
               matching_rows.emplace_back(rows);
            }
         }
         return roaring_util::fastUnion(matching_rows);
      });
   }

   /// The rows that are null, viewing into this column.
   [[nodiscard]] Bitmap nullRows() const;

   [[nodiscard]] const std::vector<DictionaryEncodedColumnChunk>& getChunks() const {
      return chunks;
   }

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   void update(const roaring::Roaring& row_ids, const Buffer::value_type& value);

   /// Assigns `new_values[i]` to the `i`-th row (in ascending order) of `rows` within the chunk
   /// `chunk_id`; a `std::nullopt` value marks the row null. Rows not in `rows` are left untouched.
   void update(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView rows,
      const Buffer& new_values
   );

   [[nodiscard]] size_t numChunks() const { return chunks.size(); }

   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const { return chunks.at(chunk_id).size(); }

   [[nodiscard]] const Idx& getValue(RowId row_id) const {
      return chunks[row_id.chunk_id].value_ids[row_id.row_in_chunk];
   }

   [[nodiscard]] bool isNull(RowId row_id) const {
      return chunks[row_id.chunk_id].isNull(row_id.row_in_chunk);
   }

   [[nodiscard]] std::string getValueString(RowId row_id) const
      requires(Values::IS_STRING)
   {
      return lookupValue(getValue(row_id));
   }

   [[nodiscard]] auto lookupValue(Idx dict_id) const {
      return Values::read(dictionary, RowId::fromGlobal(dict_id));
   }

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
   /// String keys view into `buffer`. Inserting renumbers existing ids, so the chunks are remapped
   /// before this returns.
   Values::KeyMap internValues(const Buffer& buffer);

   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & dictionary;
      archive & chunks;
      // clang-format on
   }
};

extern template class DictionaryEncodedColumn<StringColumn>;
extern template class DictionaryEncodedColumn<Date32Column>;

}  // namespace rhydb::storage::column
