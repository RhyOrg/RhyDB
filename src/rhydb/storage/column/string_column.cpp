#include "rhydb/storage/column/string_column.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rhydb/common/bidirectional_string_map.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/common/tree_node_id.h"
#include "rhydb/initialize/initialize_exception.h"

using rhydb::common::TreeNodeId;

namespace rhydb::storage::column {

size_t StringColumnChunk::insert(std::string_view value) {
   if (value.size() <= RhyDBString::SHORT_STRING_SIZE) {
      return fixed_string_data.insert(RhyDBString{value});
   }
   RHYDB_ASSERT(value.length() < UINT32_MAX);
   auto suffix_id = variable_string_data.insert(value.substr(RhyDBString::PREFIX_LENGTH));
   return fixed_string_data.insert(RhyDBString{
      static_cast<uint32_t>(value.length()), value.substr(0, RhyDBString::PREFIX_LENGTH), suffix_id,
   });
}

size_t StringColumnChunk::insertNull() {
   return fixed_string_data.insert(RhyDBString(""));
}

size_t StringColumnChunk::numValues() const {
   return fixed_string_data.numValues();
}

RhyDBString StringColumnChunk::getValue(size_t row_in_chunk) const {
   return fixed_string_data.get(row_in_chunk);
}

std::string StringColumnChunk::lookupValue(RhyDBString string) const {
   if (string.isInPlace()) {
      auto string_view = string.getShortString();
      return std::string{string_view};
   }
   std::string result;
   result.reserve(string.length());
   result += string.prefix();

   auto suffix_id = string.suffixId();
   const vector::VariableDataRegistry::DataList suffix_chunks = variable_string_data.get(suffix_id);
   const vector::VariableDataRegistry::DataList* current_chunk = &suffix_chunks;
   while (current_chunk) {
      result += current_chunk->data;
      current_chunk = current_chunk->continuation.get();
   }
   return result;
}

StringColumn::StringColumn(StringColumnMetadata* metadata)
    : metadata(metadata) {}

RhyDBString StringColumn::getValue(RowId row_id) const {
   return chunks[row_id.chunk_id].getValue(row_id.row_in_chunk);
}

std::string StringColumn::getValueString(RowId row_id) const {
   const auto& chunk = chunks[row_id.chunk_id];
   return chunk.lookupValue(chunk.getValue(row_id.row_in_chunk));
}

roaring::Roaring StringColumn::getDescendants(const TreeNodeId& parent) const {
   if (!metadata->phylo_tree.has_value()) {
      return {};
   }
   return metadata->phylo_tree->getDescendants(parent);
}

namespace {
// Binds every phylo-tree leaf referenced in `buffer` to its global row id (`base + i`) atomically:
// the whole buffer is validated first, and the bindings are applied only once all of them are known
// to be valid. On failure nothing is mutated, so the caller can treat the tree as unchanged.
std::expected<void, std::string> registerPhyloNodes(
   const StringColumn::Buffer& buffer,
   size_t base,
   StringColumnMetadata* metadata
) {
   if (!metadata->phylo_tree.has_value()) {
      return {};
   }
   std::vector<std::pair<common::TreeNode*, size_t>> pending_bindings;
   // Tracks nodes already claimed earlier in this same buffer; `rowIndexExists()` cannot catch
   // these because the validated bindings have not been applied to the tree yet.
   std::unordered_set<common::TreeNode*> claimed_in_buffer;
   for (size_t i = 0; i < buffer.size(); ++i) {
      const auto& value = buffer[i];
      if (!value.has_value()) {
         continue;
      }
      auto child_it = (metadata->phylo_tree->nodes).find(TreeNodeId{*value});
      if (child_it == metadata->phylo_tree->nodes.end()) {
         continue;
      }
      common::TreeNode* node = child_it->second.get();
      if (node->rowIndexExists() || !claimed_in_buffer.insert(node).second) {
         return std::unexpected(
            fmt::format("Node '{}' already exists in the phylogenetic tree.", *value)
         );
      }
      pending_bindings.emplace_back(node, base + i);
   }
   // All bindings validated; apply them. This loop cannot fail.
   for (const auto& [node, row_id] : pending_bindings) {
      node->row_index = row_id;
   }
   return {};
}
}  // namespace

std::expected<void, std::string> StringColumn::appendChunk(const Buffer& buffer) {
   // Build the chunk in isolation so that previously appended chunks are never touched.
   // `registerPhyloNodes` is the only fallible step and applies its tree mutations atomically, so
   // it runs before any change to `null_bitmap`/`chunks`; on failure the column stays unmodified.
   const uint32_t base = RowId::chunkStart(static_cast<uint16_t>(chunks.size()));
   if (auto result = registerPhyloNodes(buffer, base, metadata); !result.has_value()) {
      return result;
   }
   StringColumnChunk chunk;
   for (size_t i = 0; i < buffer.size(); ++i) {
      const auto& value = buffer[i];
      if (value.has_value()) {
         chunk.insert(*value);
      } else {
         null_bitmap.add(base + i);
         chunk.insertNull();
      }
   }
   chunks.push_back(std::move(chunk));
   return {};
}

void StringColumn::update(
   const roaring::Roaring& row_ids,
   const std::optional<std::string>& value
) {
   // Chunks are immutable once appended, so every chunk containing an updated row is rebuilt from
   // scratch rather than mutated in place. First collect the chunk ids that are actually touched so
   // untouched chunks are left alone.
   std::unordered_set<uint16_t> touched_chunk_ids;
   for (const uint32_t global_row_id : row_ids) {
      touched_chunk_ids.insert(RowId::fromGlobal(global_row_id).chunk_id);
   }

   for (const uint16_t chunk_id : touched_chunk_ids) {
      const uint32_t chunk_row_count = chunkSize(chunk_id);
      StringColumnChunk rebuilt_chunk;
      for (uint32_t row_in_chunk = 0; row_in_chunk < chunk_row_count; ++row_in_chunk) {
         const RowId row_id{
            .chunk_id = chunk_id, .row_in_chunk = static_cast<uint16_t>(row_in_chunk),
         };
         const uint32_t global_row_id = row_id.toGlobal();
         if (row_ids.contains(global_row_id)) {
            // Updated row: take the new value (or the null placeholder when clearing).
            if (value.has_value()) {
               rebuilt_chunk.insert(*value);
            } else {
               rebuilt_chunk.insertNull();
            }
         } else if (null_bitmap.contains(global_row_id)) {
            // Untouched null row: reads from the old chunk are meaningless, keep it null.
            rebuilt_chunk.insertNull();
         } else {
            // Untouched non-null row: copy its current value over from the old chunk.
            rebuilt_chunk.insert(getValueString(row_id));
         }
      }
      chunks.at(chunk_id) = std::move(rebuilt_chunk);
   }

   if (value.has_value()) {
      null_bitmap -= row_ids;
   } else {
      null_bitmap |= row_ids;
   }
}

bool StringColumn::isNull(RowId row_id) const {
   return null_bitmap.contains(row_id.toGlobal());
}

void StringColumnBuilder::insert(std::string_view value) {
   buffer.emplace_back(std::string{value});
}

void StringColumnBuilder::insertNull() {
   buffer.emplace_back(std::nullopt);
}

size_t StringColumnBuilder::numValues() const {
   return buffer.size();
}

StringColumn::Buffer StringColumnBuilder::finalize() {
   StringColumn::Buffer result = std::move(buffer);
   buffer.clear();
   return result;
}

}  // namespace rhydb::storage::column
