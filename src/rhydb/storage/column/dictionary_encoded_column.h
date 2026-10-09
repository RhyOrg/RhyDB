#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/assume_abstract.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/bidirectional_string_map.h"
#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/types.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/chunked_value_buffer.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/lineage_index.h"

namespace rhydb::storage::column {

class DictionaryEncodedColumnBuilder;

class DictionaryEncodedColumnMetadata : public ColumnMetadata {
  public:
   common::BidirectionalStringMap dictionary;
   std::optional<common::LineageTreeAndIdMap> lineage_tree;
   bool treat_unknown_lineages_as_null = false;

   explicit DictionaryEncodedColumnMetadata(std::string column_name)
       : ColumnMetadata(std::move(column_name)) {}

   DictionaryEncodedColumnMetadata(
      std::string column_name,
      rhydb::common::BidirectionalStringMap dictionary
   )
       : ColumnMetadata(std::move(column_name)),
         dictionary(std::move(dictionary)) {}

   DictionaryEncodedColumnMetadata(
      std::string column_name,
      common::LineageTreeAndIdMap lineage_tree_and_id_map,
      bool treat_unknown_lineages_as_null
   );

   DictionaryEncodedColumnMetadata(
      std::string column_name,
      rhydb::common::BidirectionalStringMap dictionary,
      common::LineageTreeAndIdMap lineage_tree_and_id_map,
      bool treat_unknown_lineages_as_null
   );

   DictionaryEncodedColumnMetadata() = delete;
   DictionaryEncodedColumnMetadata(const DictionaryEncodedColumnMetadata& other) = delete;
   DictionaryEncodedColumnMetadata(DictionaryEncodedColumnMetadata&& other) = delete;
   DictionaryEncodedColumnMetadata& operator=(const DictionaryEncodedColumnMetadata& other) =
      delete;
   DictionaryEncodedColumnMetadata& operator=(DictionaryEncodedColumnMetadata&& other) = delete;
};

class DictionaryEncodedColumn {
  public:
   using Metadata = DictionaryEncodedColumnMetadata;
   using Builder = DictionaryEncodedColumnBuilder;
   using Buffer = std::vector<std::optional<std::string>>;

   static constexpr schema::ColumnType TYPE = schema::ColumnType::DICTIONARY_ENCODED;
   static constexpr schema::ValueType type() { return schema::ValueType::STRING; }
   using value_type = std::string_view;

   Metadata* metadata;
   roaring::Roaring null_bitmap;

  private:
   ChunkedValueBuffer<Idx> value_ids;
   std::unordered_map<Idx, roaring::Roaring> indexed_values;
   std::optional<LineageIndex> lineage_index;

  public:
   explicit DictionaryEncodedColumn(Metadata* metadata);

   [[nodiscard]] std::optional<const roaring::Roaring*> filter(rhydb::Idx value_id) const;

   [[nodiscard]] std::optional<const roaring::Roaring*> filter(
      const std::optional<std::string>& value
   ) const;

   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   void update(const roaring::Roaring& row_ids, const std::optional<std::string>& value);

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
      return std::string{lookupValue(getValue(row_id))};
   }

   [[nodiscard]] std::string_view lookupValue(Idx dict_id) const {
      return metadata->dictionary.getValue(dict_id);
   }

   [[nodiscard]] std::optional<rhydb::Idx> getValueId(const std::string& value) const;

   [[nodiscard]] const std::optional<LineageIndex>& getLineageIndex() const;

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & value_ids;
      archive & indexed_values;
      archive & null_bitmap;
      if(lineage_index.has_value()){
         archive & lineage_index.value();
      }
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

BOOST_SERIALIZATION_SPLIT_FREE(rhydb::storage::column::DictionaryEncodedColumnMetadata);
namespace boost::serialization {
template <class Archive>
[[maybe_unused]] void save(
   Archive& archive,
   const rhydb::storage::column::DictionaryEncodedColumnMetadata& object,
   [[maybe_unused]] const uint32_t version
) {
   archive & object.column_name;
   archive & object.dictionary;
   archive & object.lineage_tree;
   archive & object.treat_unknown_lineages_as_null;
}
}  // namespace boost::serialization

BOOST_SERIALIZATION_SPLIT_FREE(
   std::shared_ptr<rhydb::storage::column::DictionaryEncodedColumnMetadata>
);
namespace boost::serialization {
template <class Archive>
[[maybe_unused]] void load(
   Archive& archive,
   std::shared_ptr<rhydb::storage::column::DictionaryEncodedColumnMetadata>& object,
   [[maybe_unused]] const uint32_t version
) {
   std::string column_name;
   rhydb::common::BidirectionalStringMap dictionary;
   std::optional<rhydb::common::LineageTreeAndIdMap> lineage_tree;
   bool treat_unknown_lineages_as_null;
   archive & column_name;
   archive & dictionary;
   archive & lineage_tree;
   archive & treat_unknown_lineages_as_null;
   if (lineage_tree.has_value()) {
      object = std::make_shared<rhydb::storage::column::DictionaryEncodedColumnMetadata>(
         std::move(column_name),
         std::move(dictionary),
         std::move(lineage_tree.value()),
         treat_unknown_lineages_as_null
      );
   } else {
      object = std::make_shared<rhydb::storage::column::DictionaryEncodedColumnMetadata>(
         std::move(column_name), std::move(dictionary)
      );
   }
}
}  // namespace boost::serialization
