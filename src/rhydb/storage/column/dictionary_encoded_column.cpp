#include "rhydb/storage/column/dictionary_encoded_column.h"

#include <optional>

#include <fmt/format.h>

#include "rhydb/common/bidirectional_string_map.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

DictionaryEncodedColumnMetadata::DictionaryEncodedColumnMetadata(
   std::string column_name,
   common::LineageTreeAndIdMap lineage_tree_and_id_map,
   bool treat_unknown_lineages_as_null
)
    : ColumnMetadata(std::move(column_name)),
      dictionary(lineage_tree_and_id_map.lineage_id_lookup_map.copy()),
      lineage_tree(std::move(lineage_tree_and_id_map)),
      treat_unknown_lineages_as_null(treat_unknown_lineages_as_null) {}

DictionaryEncodedColumnMetadata::DictionaryEncodedColumnMetadata(
   std::string column_name,
   common::BidirectionalStringMap dictionary,
   common::LineageTreeAndIdMap lineage_tree_and_id_map,
   bool treat_unknown_lineages_as_null
)
    : ColumnMetadata(std::move(column_name)),
      dictionary(std::move(dictionary)),
      lineage_tree(std::move(lineage_tree_and_id_map)),
      treat_unknown_lineages_as_null(treat_unknown_lineages_as_null) {}

DictionaryEncodedColumn::DictionaryEncodedColumn(DictionaryEncodedColumnMetadata* metadata)
    : metadata(metadata) {
   if (metadata->lineage_tree.has_value()) {
      lineage_index = LineageIndex{&metadata->lineage_tree->lineage_tree};
   }
}

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
   // Validate whole buffer before mutating anything
   if (lineage_index.has_value() && !metadata->treat_unknown_lineages_as_null) {
      for (const auto& maybe_value : buffer) {
         if (maybe_value.has_value() && !metadata->dictionary.getId(*maybe_value).has_value()) {
            return std::unexpected(
               fmt::format(
                  "The value '{}' is not a valid lineage value for column '{}'. "
                  "Is your lineage definition file outdated?",
                  *maybe_value,
                  metadata->column_name
               )
            );
         }
      }
   }

   // Build this chunk's value ids in isolation so that previously appended chunks are never
   // touched; the inverted index and lineage index are global and keep being updated by row id.
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

      if (lineage_index.has_value()) {
         const auto value_id = metadata->dictionary.getId(value);
         if (value_id.has_value()) {
            lineage_index.value().insert(row_id, value_id.value());
         }
      }

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
   RHYDB_ASSERT(!lineage_index.has_value());

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

bool DictionaryEncodedColumn::isNull(RowId row_id) const {
   return null_bitmap.contains(row_id.toGlobal());
}

std::optional<rhydb::Idx> DictionaryEncodedColumn::getValueId(const std::string& value) const {
   return metadata->dictionary.getId(value);
}

const std::optional<LineageIndex>& DictionaryEncodedColumn::getLineageIndex() const {
   return lineage_index;
}

}  // namespace rhydb::storage::column
