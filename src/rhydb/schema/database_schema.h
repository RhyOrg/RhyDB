#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

#include <spdlog/spdlog.h>
#include <boost/serialization/access.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/split_member.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/schema/data_type.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"

namespace rhydb::schema {

bool isSequenceColumn(const DataTypePtr& type);

struct ColumnIdentifier {
   std::string name;
   rhydb::schema::DataTypePtr type;

   bool operator<(const ColumnIdentifier& other) const {
      return std::tie(name, *type) < std::tie(other.name, *other.type);
   }

   bool operator==(const ColumnIdentifier& other) const {
      return std::tie(name, *type) == std::tie(other.name, *other.type);
   }

  private:
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void save(Archive& archive, const uint32_t /*version*/) const {
      archive & name;
      saveDataType(archive, *type);
   }

   template <class Archive>
   [[maybe_unused]] void load(Archive& archive, const uint32_t /*version*/) {
      archive & name;
      type = loadDataType(archive);
   }

   BOOST_SERIALIZATION_SPLIT_MEMBER()
};

class TableSchema {
  public:
   std::map<ColumnIdentifier, std::shared_ptr<storage::column::ColumnMetadata>> column_metadata;
   std::optional<ColumnIdentifier> primary_key;

   TableSchema(
      std::map<ColumnIdentifier, std::shared_ptr<storage::column::ColumnMetadata>> column_metadata,
      std::optional<ColumnIdentifier> primary_key
   )
       : column_metadata(std::move(column_metadata)),
         primary_key(std::move(primary_key)) {
      RHYDB_ASSERT(
         !this->primary_key.has_value() || this->column_metadata.contains(*this->primary_key)
      );
   }

   [[nodiscard]] std::optional<ColumnIdentifier> getColumn(std::string_view name) const;

   [[nodiscard]] std::vector<ColumnIdentifier> getColumnIdentifiers() const;

   template <rhydb::storage::column::Column ColumnType>
   [[nodiscard]] std::vector<ColumnIdentifier> getColumnByType() const {
      std::vector<ColumnIdentifier> result;
      for (const auto& [column_identifier, _] : column_metadata) {
         if (column_identifier.type == ColumnType::TYPE) {
            result.push_back(column_identifier);
         }
      }
      return result;
   }

   template <storage::column::Column ColumnType>
   [[nodiscard]] std::optional<typename ColumnType::Metadata*> getColumnMetadata(
      std::string_view name
   ) const {
      auto iter = std::ranges::find_if(column_metadata, [&name](const auto& metadata_pair) {
         return metadata_pair.first.name == name;
      });
      if (iter == column_metadata.end()) {
         return std::nullopt;
      }
      if (iter->first.type != ColumnType::TYPE) {
         SPDLOG_INFO(
            "Mismatching type found: expected {} vs actual {}",
            dataTypeToString(ColumnType::TYPE),
            dataTypeToString(iter->first.type)
         );
         return std::nullopt;
      }
      auto typed_metadata = dynamic_cast<ColumnType::Metadata*>(iter->second.get());
      RHYDB_ASSERT(typed_metadata != nullptr);
      return typed_metadata;
   }

   TableSchema() = default;

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void save(Archive& archive, unsigned int version) const;

   template <class Archive>
   void load(Archive& archive, unsigned int version);

   template <class Archive>
   void serialize(Archive& archive, const unsigned int version) {
      boost::serialization::split_member(archive, *this, version);
   }
};

// NOLINTNEXTLINE(bugprone-exception-escape): false positive in libc++ pair with llvm
class TableName {
   std::string name;

  public:
   explicit TableName(std::string name);

   [[nodiscard]] const std::string& getName() const { return name; }

   static const TableName& getDefault();

   bool operator==(const TableName& other) const { return name == other.name; }
   bool operator<(const TableName& other) const { return name < other.name; }

   TableName() = default;

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      archive & name;
   }
};

class DatabaseSchema {
  public:
   std::map<TableName, std::shared_ptr<TableSchema>> tables;

   static DatabaseSchema loadFromFile(const std::filesystem::path& file_path);
   void saveToFile(const std::filesystem::path& file_path) const;
};

}  // namespace rhydb::schema

namespace boost::serialization {

template <class Archive>
void save(
   Archive& archive,
   const std::shared_ptr<rhydb::schema::TableSchema>& ptr,
   const unsigned int /*version*/
) {
   archive << *ptr;
}

template <class Archive>
void load(
   Archive& archive,
   std::shared_ptr<rhydb::schema::TableSchema>& ptr,
   const unsigned int /*version*/
) {
   ptr = std::make_shared<rhydb::schema::TableSchema>();
   archive >> *ptr;
}

}  // namespace boost::serialization

BOOST_SERIALIZATION_SPLIT_FREE(std::shared_ptr<rhydb::schema::TableSchema>)
