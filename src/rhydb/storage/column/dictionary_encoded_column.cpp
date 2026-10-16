#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <optional>

#include <fmt/format.h>

#include "rhydb/common/bidirectional_string_map.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

DictionaryEncodedColumn::DictionaryEncodedColumn(DictionaryEncodedColumnMetadata* metadata)
    : metadata(metadata) {}

std::optional<const roaring::Roaring*> DictionaryEncodedColumn::filter(Idx value_id) const {
   if (indexed_values.contains(value_id)) {
      return &indexed_values.at(value_id);
   }
   return std::nullopt;
}

std::optional<const roaring::Roaring*> DictionaryEncodedColumn::filter(
   const std::optional<std::string>& value
) const {
   if (value == std::nullopt) {
      return &null_bitmap;
   }
   const auto& value_id = metadata->dictionary.getId(value.value());
   if (!value_id.has_value()) {
      return std::nullopt;
   }
   return filter(value_id.value());
}

std::expected<void, std::string> DictionaryEncodedColumn::appendChunk(const Buffer& buffer) {
   // Build this chunk's value ids in isolation so that previously appended chunks are never
   // touched; the inverted index is global and keeps being updated by row id.
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
         const Idx value_id = metadata->dictionary.getOrCreateId("");
         indexed_values.try_emplace(value_id);
         chunk.push_back(value_id);
         continue;
      }
      const std::string_view value = *maybe_value;
      const Idx value_id = metadata->dictionary.getOrCreateId(value);

      indexed_values[value_id].add(row_id);
      chunk.push_back(value_id);
   }
   value_ids.appendChunk(std::move(chunk));
   return {};
}

void DictionaryEncodedColumn::update(
   const roaring::Roaring& row_ids,
   const std::optional<std::string>& value
) {
   // Null rows carry the empty-string placeholder id as their stored value (see `appendChunk`), so
   // the target id is that placeholder for a null update and the interned value id otherwise.
   const Idx new_value_id = value.has_value() ? metadata->dictionary.getOrCreateId(*value)
                                              : metadata->dictionary.getOrCreateId("");
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

void DictionaryEncodedColumn::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));

   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      const RowId row_id{.chunk_id = chunk_id, .row_in_chunk = row_in_chunk};
      const uint32_t global_row_id = row_id.toGlobal();
      const std::optional<std::string>& value = new_values[index++];
      // As in the single-value `update`: a null row stores the empty-string placeholder id and is
      // absent from every match bitmap.
      const Idx new_value_id = metadata->dictionary.getOrCreateId(value.value_or(""));
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

bool DictionaryEncodedColumn::isNull(RowId row_id) const {
   return null_bitmap.contains(row_id.toGlobal());
}

std::optional<rhydb::Idx> DictionaryEncodedColumn::getValueId(const std::string& value) const {
   return metadata->dictionary.getId(value);
}

}  // namespace rhydb::storage::column
