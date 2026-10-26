#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <algorithm>
#include <compare>
#include <optional>
#include <vector>

#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

template <DictionaryValueColumn ValueColumn>
DictionaryEncodedColumn<ValueColumn>::DictionaryEncodedColumn(Metadata* metadata)
    : metadata(metadata),
      dictionary_metadata(std::make_unique<typename ValueColumn::Metadata>(metadata->column_name)),
      dictionary(dictionary_metadata.get()) {}

template <DictionaryValueColumn ValueColumn>
size_t DictionaryEncodedColumn<ValueColumn>::numDictionaryValues() const {
   size_t num_values = 0;
   for (size_t chunk_id = 0; chunk_id < dictionary.numChunks(); ++chunk_id) {
      num_values += dictionary.chunkSize(chunk_id);
   }
   return num_values;
}

template <DictionaryValueColumn ValueColumn>
std::optional<Idx> DictionaryEncodedColumn<ValueColumn>::findDictionaryId(Values::Key value) const {
   // `insertDictionaryValues` fills every chunk but the last one completely, so the ids are
   // contiguous
   Idx low = 0;
   auto high = static_cast<Idx>(numDictionaryValues());
   while (low < high) {
      const Idx mid = low + ((high - low) / 2);
      const auto ordering = Values::compareAt(dictionary, RowId::fromGlobal(mid), value);
      if (ordering == std::strong_ordering::equal) {
         return mid;
      }
      if (ordering == std::strong_ordering::less) {
         low = mid + 1;
      } else {
         high = mid;
      }
   }
   return std::nullopt;
}

template <DictionaryValueColumn ValueColumn>
std::vector<Idx> DictionaryEncodedColumn<ValueColumn>::insertDictionaryValues(
   const std::vector<typename Values::Key>& new_values
) {
   const auto old_num_values = static_cast<Idx>(numDictionaryValues());
   std::vector<Idx> old_to_new_id;
   old_to_new_id.reserve(old_num_values);

   ValueColumn merged{dictionary_metadata.get()};
   typename ValueColumn::Buffer chunk;
   chunk.reserve(COLUMN_CHUNK_SIZE);
   const auto flush = [&] {
      const auto appended = merged.appendChunk(chunk);
      RHYDB_ASSERT(appended.has_value());
      chunk.clear();
   };
   const auto push = [&](auto value) {
      chunk.emplace_back(std::move(value));
      if (chunk.size() == COLUMN_CHUNK_SIZE) {
         flush();
      }
   };

   Idx next_id = 0;
   auto new_value = new_values.begin();
   for (Idx old_id = 0; old_id < old_num_values; ++old_id) {
      auto old_value = lookupValue(old_id);
      for (; new_value != new_values.end() && Values::compare(*new_value, old_value) < 0;
           ++new_value) {
         push(*new_value);
         ++next_id;
      }
      RHYDB_ASSERT(new_value == new_values.end() || Values::compare(*new_value, old_value) != 0);
      push(std::move(old_value));
      old_to_new_id.push_back(next_id++);
   }
   for (; new_value != new_values.end(); ++new_value) {
      push(*new_value);
   }
   if (!chunk.empty()) {
      flush();
   }

   dictionary = std::move(merged);
   return old_to_new_id;
}

template <DictionaryValueColumn ValueColumn>
std::optional<const roaring::Roaring*> DictionaryEncodedColumn<ValueColumn>::filterById(
   Idx value_id
) const {
   if (indexed_values.contains(value_id)) {
      return &indexed_values.at(value_id);
   }
   return std::nullopt;
}

template <DictionaryValueColumn ValueColumn>
std::optional<const roaring::Roaring*> DictionaryEncodedColumn<ValueColumn>::filter(
   std::optional<Key> value
) const {
   if (value == std::nullopt) {
      return &null_bitmap;
   }
   const auto& value_id = findDictionaryId(value.value());
   if (!value_id.has_value()) {
      return std::nullopt;
   }
   return filterById(value_id.value());
}

template <DictionaryValueColumn ValueColumn>
DictionaryValues<ValueColumn>::KeyMap DictionaryEncodedColumn<ValueColumn>::internValues(
   const Buffer& buffer
) {
   // Deduplicate by hash first, so that only the distinct values need to be sorted and searched
   typename Values::KeyMap value_ids_by_value;
   for (const auto& value : buffer) {
      value_ids_by_value.try_emplace(value.has_value() ? Key{*value} : Key{}, 0);
   }

   std::vector<Key> missing_values;
   for (auto& [value, value_id] : value_ids_by_value) {
      if (auto existing_id = findDictionaryId(value)) {
         value_id = *existing_id;
      } else {
         missing_values.push_back(value);
      }
   }
   if (missing_values.empty()) {
      return value_ids_by_value;
   }
   std::ranges::sort(missing_values, [](Key lhs, Key rhs) {
      return Values::compare(lhs, rhs) < 0;
   });

   const std::vector<Idx> old_to_new_id = insertDictionaryValues(missing_values);
   for (size_t chunk_id = 0; chunk_id < value_ids.numChunks(); ++chunk_id) {
      for (uint32_t row_in_chunk = 0; row_in_chunk < value_ids.chunkSize(chunk_id);
           ++row_in_chunk) {
         const RowId row_id{
            .chunk_id = static_cast<uint16_t>(chunk_id),
            .row_in_chunk = static_cast<uint16_t>(row_in_chunk),
         };
         value_ids.setValue(row_id, old_to_new_id.at(value_ids.at(row_id)));
      }
   }
   std::unordered_map<Idx, roaring::Roaring> remapped_indexed_values;
   for (auto& [old_id, bitmap] : indexed_values) {
      remapped_indexed_values.emplace(old_to_new_id.at(old_id), std::move(bitmap));
   }
   indexed_values = std::move(remapped_indexed_values);

   // The ids of the values that were already present have shifted as well, so look all up anew
   for (auto& [value, value_id] : value_ids_by_value) {
      value_id = findDictionaryId(value).value();
   }
   return value_ids_by_value;
}

template <DictionaryValueColumn ValueColumn>
std::expected<void, std::string> DictionaryEncodedColumn<ValueColumn>::appendChunk(
   const Buffer& buffer
) {
   const auto ids_by_value = internValues(buffer);
   const uint32_t base = RowId::chunkStart(static_cast<uint16_t>(value_ids.numChunks()));
   std::vector<Idx> chunk;
   chunk.reserve(buffer.size());
   for (size_t i = 0; i < buffer.size(); ++i) {
      const uint32_t row_id = base + static_cast<uint32_t>(i);
      const auto& maybe_value = buffer[i];
      if (!maybe_value.has_value()) {
         null_bitmap.add(row_id);
         // We need to add something to the vector, so that the size of the vector remains equal to
         // row_id but we do not add our row_id to indexed_values[value_id]
         const Idx value_id = ids_by_value.at({});
         indexed_values.try_emplace(value_id);
         chunk.push_back(value_id);
         continue;
      }
      const Idx value_id = ids_by_value.at(*maybe_value);

      indexed_values[value_id].add(row_id);
      chunk.push_back(value_id);
   }
   value_ids.appendChunk(std::move(chunk));
   return {};
}

template <DictionaryValueColumn ValueColumn>
void DictionaryEncodedColumn<ValueColumn>::update(
   const roaring::Roaring& row_ids,
   const Buffer::value_type& value
) {
   // Null rows carry the placeholder id as their stored value (see `appendChunk`), so the target
   // id is that placeholder for a null update and the interned value id otherwise.
   const Buffer new_values{value};
   const Idx new_value_id = internValues(new_values).at(value.has_value() ? Key{*value} : Key{});
   // `try_emplace` keeps the id present in the index even for the null placeholder, matching how
   // `appendChunk` treats null rows (present as a value id, absent from any match bitmap).
   indexed_values.try_emplace(new_value_id);

   for (const uint32_t global_row_id : row_ids) {
      const RowId row_id = RowId::fromGlobal(global_row_id);
      // Detach the row from its previous value's match bitmap. Previously-null rows are not members
      // of any match bitmap, so only non-null rows need detaching.
      if (!null_bitmap.contains(global_row_id)) {
         const Idx old_value_id = value_ids.at(row_id);
         indexed_values.at(old_value_id).remove(global_row_id);
      }
      value_ids.setValue(row_id, new_value_id);
      if (value.has_value()) {
         indexed_values.at(new_value_id).add(global_row_id);
      }
   }

   if (value.has_value()) {
      null_bitmap -= row_ids;
   } else {
      null_bitmap |= row_ids;
   }
}

template <DictionaryValueColumn ValueColumn>
void DictionaryEncodedColumn<ValueColumn>::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));

   const auto ids_by_value = internValues(new_values);
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const uint32_t global_row_id = row_id.toGlobal();
      const auto& value = new_values[index++];
      // As in the single-value `update`: a null row stores the placeholder id and is absent from
      // every match bitmap.
      const Idx new_value_id = ids_by_value.at(value.has_value() ? Key{*value} : Key{});
      indexed_values.try_emplace(new_value_id);

      if (!null_bitmap.contains(global_row_id)) {
         indexed_values.at(value_ids.at(row_id)).remove(global_row_id);
      }
      value_ids.setValue(row_id, new_value_id);
      if (value.has_value()) {
         indexed_values.at(new_value_id).add(global_row_id);
         null_bitmap.remove(global_row_id);
      } else {
         null_bitmap.add(global_row_id);
      }
   }
}

template <DictionaryValueColumn ValueColumn>
bool DictionaryEncodedColumn<ValueColumn>::isNull(RowId row_id) const {
   return null_bitmap.contains(row_id.toGlobal());
}

template <DictionaryValueColumn ValueColumn>
std::optional<rhydb::Idx> DictionaryEncodedColumn<ValueColumn>::getValueId(
   Values::Key value
) const {
   return findDictionaryId(value);
}

template class DictionaryEncodedColumn<StringColumn>;
template class DictionaryEncodedColumn<Date32Column>;

}  // namespace rhydb::storage::column
