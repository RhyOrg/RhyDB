#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <algorithm>
#include <compare>
#include <iterator>
#include <optional>
#include <vector>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

void DictionaryEncodedColumnChunk::setValueId(uint16_t row_in_chunk, Idx value_id, bool is_null) {
   // Detach the row from its previous value's rows. A null row is not in any of them
   if (!null_rows.removeChecked(row_in_chunk)) {
      const auto previous_rows = rows_by_value_id.find(value_ids.at(row_in_chunk));
      RHYDB_ASSERT(previous_rows != rows_by_value_id.end());
      previous_rows->second.removeChecked(row_in_chunk);
      if (previous_rows->second.empty()) {
         rows_by_value_id.erase(previous_rows);
      }
   }
   value_ids.at(row_in_chunk) = value_id;
   if (is_null) {
      null_rows.add(row_in_chunk);
   } else {
      addToRowsByValueId(row_in_chunk, value_id);
   }
}

void DictionaryEncodedColumnChunk::addToRowsByValueId(uint16_t row_in_chunk, Idx value_id) {
   // Look up first, so that only a missing value id pays for constructing a container
   auto rows = rows_by_value_id.find(value_id);
   if (rows == rows_by_value_id.end()) {
      rows =
         rows_by_value_id.emplace(value_id, roaring_util::RoaringContainer::withCapacity(0)).first;
   }
   rows->second.add(row_in_chunk);
}

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
Bitmap DictionaryEncodedColumn<ValueColumn>::filterById(Idx value_id) const {
   return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
      const auto& rows_by_value_id = chunks[chunk_id].rows_by_value_id;
      const auto rows = rows_by_value_id.find(value_id);
      if (rows == rows_by_value_id.end()) {
         // An empty view, which `fromChunks` skips
         return roaring_util::RoaringContainerView{nullptr, 0, ARRAY_CONTAINER_TYPE};
      }
      return roaring_util::RoaringContainerView{rows->second};
   });
}

template <DictionaryValueColumn ValueColumn>
std::optional<Bitmap> DictionaryEncodedColumn<ValueColumn>::filter(Key value) const {
   const auto& value_id = findDictionaryId(value);
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
   for (auto& chunk : chunks) {
      for (Idx& value_id : chunk.value_ids) {
         value_id = old_to_new_id.at(value_id);
      }
      std::unordered_map<Idx, roaring_util::RoaringContainer> remapped_rows_by_value_id;
      remapped_rows_by_value_id.reserve(chunk.rows_by_value_id.size());
      for (auto& [old_id, rows] : chunk.rows_by_value_id) {
         remapped_rows_by_value_id.emplace(old_to_new_id.at(old_id), std::move(rows));
      }
      chunk.rows_by_value_id = std::move(remapped_rows_by_value_id);
   }

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
   DictionaryEncodedColumnChunk chunk;
   chunk.value_ids.reserve(buffer.size());
   for (size_t i = 0; i < buffer.size(); ++i) {
      const auto row_in_chunk = static_cast<uint16_t>(i);
      const auto& maybe_value = buffer[i];
      if (!maybe_value.has_value()) {
         // A null row still holds an id, the placeholder's, so that every row has one
         chunk.null_rows.add(row_in_chunk);
         chunk.value_ids.push_back(ids_by_value.at({}));
         continue;
      }
      const Idx value_id = ids_by_value.at(*maybe_value);
      chunk.addToRowsByValueId(row_in_chunk, value_id);
      chunk.value_ids.push_back(value_id);
   }
   chunks.push_back(std::move(chunk));
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
   for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
      auto& chunk = chunks.at(chunk_id);
      for (const uint16_t row_in_chunk : rows) {
         chunk.setValueId(row_in_chunk, new_value_id, !value.has_value());
      }
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
   auto& chunk = chunks.at(chunk_id);
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const auto& value = new_values[index++];
      // As in the single-value `update`: a null row stores the placeholder id
      const Idx new_value_id = ids_by_value.at(value.has_value() ? Key{*value} : Key{});
      chunk.setValueId(row_in_chunk, new_value_id, !value.has_value());
   }
}

template <DictionaryValueColumn ValueColumn>
Bitmap DictionaryEncodedColumn<ValueColumn>::nullRows() const {
   return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
      return roaring_util::RoaringContainerView{chunks[chunk_id].null_rows};
   });
}

template <DictionaryValueColumn ValueColumn>
void DictionaryEncodedColumn<ValueColumn>::deleteRows(const RowCompaction& compaction) {
   for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      const auto old_chunk_id = static_cast<uint16_t>(chunk_id);
      if (!compaction.losesSomeRows(old_chunk_id)) {
         continue;
      }
      auto& chunk = chunks[chunk_id];
      const uint32_t remaining_rows =
         compaction.compactRows(old_chunk_id, chunk.size(), [&](uint32_t from, uint32_t to) {
            chunk.value_ids[to] = chunk.value_ids[from];
         });
      chunk.value_ids.resize(remaining_rows);
      compaction.compact(old_chunk_id, chunk.null_rows);
      for (auto entry = chunk.rows_by_value_id.begin(); entry != chunk.rows_by_value_id.end();) {
         compaction.compact(old_chunk_id, entry->second);
         entry = entry->second.empty() ? chunk.rows_by_value_id.erase(entry) : std::next(entry);
      }
   }
   compaction.dropDeletedChunks(chunks);
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
