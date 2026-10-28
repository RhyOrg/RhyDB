#include "rhydb/storage/column/bool_column.h"

#include <utility>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/row_id.h"

namespace rhydb::storage::column {

void BoolColumnChunk::setValue(uint16_t row_in_chunk, std::optional<bool> value) {
   if (value.has_value()) {
      null_rows.removeChecked(row_in_chunk);
   } else {
      null_rows.addChecked(row_in_chunk);
   }
   if (value == true) {
      true_rows.addChecked(row_in_chunk);
   } else {
      true_rows.removeChecked(row_in_chunk);
   }
   if (value == false) {
      false_rows.addChecked(row_in_chunk);
   } else {
      false_rows.removeChecked(row_in_chunk);
   }
}

BoolColumn::BoolColumn(ColumnMetadata* metadata)
    : metadata(metadata) {}

Bitmap BoolColumn::trueRows() const {
   return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
      return roaring_util::RoaringContainerView{chunks[chunk_id].true_rows};
   });
}

Bitmap BoolColumn::falseRows() const {
   return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
      return roaring_util::RoaringContainerView{chunks[chunk_id].false_rows};
   });
}

Bitmap BoolColumn::nullRows() const {
   return Bitmap::fromChunks(chunks.size(), [&](size_t chunk_id) {
      return roaring_util::RoaringContainerView{chunks[chunk_id].null_rows};
   });
}

std::expected<void, std::string> BoolColumn::appendChunk(const Buffer& buffer) {
   BoolColumnChunk chunk;
   for (size_t i = 0; i < buffer.size(); ++i) {
      const auto& value = buffer[i];
      if (!value.has_value()) {
         chunk.null_rows.add(static_cast<uint16_t>(i));
      } else if (*value) {
         chunk.true_rows.add(static_cast<uint16_t>(i));
      } else {
         chunk.false_rows.add(static_cast<uint16_t>(i));
      }
   }
   chunks.push_back(std::move(chunk));
   return {};
}

void BoolColumn::update(const roaring::Roaring& row_ids, std::optional<bool> value) {
   for (const auto& [chunk_id, rows] : Bitmap{&row_ids}) {
      auto& chunk = chunks.at(chunk_id);
      if (value.has_value()) {
         chunk.null_rows -= rows;
      } else {
         chunk.null_rows |= rows;
      }
      if (value == true) {
         chunk.true_rows |= rows;
      } else {
         chunk.true_rows -= rows;
      }
      if (value == false) {
         chunk.false_rows |= rows;
      } else {
         chunk.false_rows -= rows;
      }
   }
}

void BoolColumn::update(
   uint16_t chunk_id,
   roaring_util::RoaringContainerView rows,
   const Buffer& new_values
) {
   RHYDB_ASSERT_EQ(new_values.size(), static_cast<size_t>(rows.getCardinality()));
   auto& chunk = chunks.at(chunk_id);
   size_t index = 0;
   for (const uint16_t row_in_chunk : rows) {
      chunk.setValue(row_in_chunk, new_values[index++]);
   }
}

void BoolColumn::deleteRows(const RowCompaction& compaction) {
   for (size_t chunk_id = 0; chunk_id < chunks.size(); ++chunk_id) {
      if (!compaction.losesSomeRows(static_cast<uint16_t>(chunk_id))) {
         continue;
      }
      auto& chunk = chunks[chunk_id];
      compaction.compact(static_cast<uint16_t>(chunk_id), chunk.true_rows);
      compaction.compact(static_cast<uint16_t>(chunk_id), chunk.false_rows);
      compaction.compact(static_cast<uint16_t>(chunk_id), chunk.null_rows);
   }
   compaction.dropDeletedChunks(chunks);
}

}  // namespace rhydb::storage::column
