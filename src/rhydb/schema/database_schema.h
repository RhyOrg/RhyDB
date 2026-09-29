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
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"

namespace rhydb::schema {

enum class ColumnType : uint8_t {
   STRING,
   DICTIONARY_ENCODED,
   DATE32,
   BOOL,
   INT32,
   INT64,
   FLOAT,
   AMINO_ACID_SEQUENCE,
   NUCLEOTIDE_SEQUENCE,
   ZSTD_COMPRESSED_STRING,
};

/// The logical type of the values a column holds, as opposed to the storage-specific `ColumnType`
/// (e.g. both `STRING` and `DICTIONARY_ENCODED` columns hold `ValueType::STRING` values). This is
/// the type a column reports via `type()`.
enum class ValueType : uint8_t {
   STRING,
   DATE,
   BOOL,
   INT32,
   INT64,
   FLOAT,
   NUCLEOTIDE_SEQUENCE,
   AMINO_ACID_SEQUENCE
};

constexpr std::string_view columnTypeToString(ColumnType type) {
   switch (type) {
      case ColumnType::STRING:
         return "STRING";
      case ColumnType::DICTIONARY_ENCODED:
         return "DICTIONARY_ENCODED";
      case ColumnType::DATE32:
         return "DATE32";
      case ColumnType::BOOL:
         return "BOOL";
      case ColumnType::INT32:
         return "INT32";
      case ColumnType::INT64:
         return "INT64";
      case ColumnType::FLOAT:
         return "FLOAT";
      case ColumnType::AMINO_ACID_SEQUENCE:
         return "AMINO_ACID_SEQUENCE";
      case ColumnType::NUCLEOTIDE_SEQUENCE:
         return "NUCLEOTIDE_SEQUENCE";
      case ColumnType::ZSTD_COMPRESSED_STRING:
         return "ZSTD_COMPRESSED_STRING";
   }
   RHYDB_UNREACHABLE();
}

constexpr std::string_view valueTypeToString(ValueType type) {
   switch (type) {
      case ValueType::STRING:
         return "string";
      case ValueType::DATE:
         return "date";
      case ValueType::BOOL:
         return "boolean";
      case ValueType::INT32:
         return "int";
      case ValueType::INT64:
         return "int64";
      case ValueType::FLOAT:
         return "float";
      case ValueType::NUCLEOTIDE_SEQUENCE:
         return "nucleotideSequence";
      case ValueType::AMINO_ACID_SEQUENCE:
         return "aminoAcidSequence";
   }
   RHYDB_UNREACHABLE();
}

bool isSequenceColumn(ColumnType type);

struct ColumnIdentifier {
   std::string name;
   rhydb::schema::ColumnType type;

   bool operator<(const ColumnIdentifier& other) const {
      return std::tie(name, type) < std::tie(other.name, other.type);
   }

   bool operator==(const ColumnIdentifier& other) const {
      return std::tie(name, type) == std::tie(other.name, other.type);
   }

  private:
   friend class boost::serialization::access;
   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      archive & name;
      archive & type;
   }
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
      if (iter == column_metadata.end() || iter->first.type != ColumnType::TYPE) {
         SPDLOG_INFO(
            "Mismatching type found: expected {} vs actual {}",
            columnTypeToString(ColumnType::TYPE),
            columnTypeToString(iter->first.type)
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
