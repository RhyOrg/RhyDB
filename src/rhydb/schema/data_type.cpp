#include "rhydb/schema/data_type.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "rhydb/common/panic.h"

namespace rhydb::schema {

DataTypePtr SequenceType::get(Kind kind, uint32_t length) {
   RHYDB_ASSERT(isSequenceKind(kind));
   // The instances, so that every sequence type exists exactly once
   static std::mutex mutex;
   static std::map<std::pair<Kind, uint32_t>, DataTypePtr> instances;
   const std::scoped_lock lock{mutex};
   auto& instance = instances[{kind, length}];
   if (instance == nullptr) {
      instance = kind == Kind::NUCLEOTIDE_SEQUENCE ? make<NucleotideSequenceType>(length)
                                                   : make<AminoAcidSequenceType>(length);
   }
   return instance;
}

DataTypePtr nonParameterizedDataType(DataType::Kind kind) {
   using Kind = DataType::Kind;
   switch (kind) {
      case Kind::STRING:
         return DataType::STRING;
      case Kind::DATE32:
         return DataType::DATE32;
      case Kind::BOOL:
         return DataType::BOOL;
      case Kind::INT32:
         return DataType::INT32;
      case Kind::INT64:
         return DataType::INT64;
      case Kind::FLOAT:
         return DataType::FLOAT;
      case Kind::ZSTD_COMPRESSED_STRING:
         return DataType::ZSTD_COMPRESSED_STRING;
      case Kind::DICTIONARY_ENCODED:
      case Kind::AMINO_ACID_SEQUENCE:
      case Kind::NUCLEOTIDE_SEQUENCE:
         break;
   }
   return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
std::string dataTypeToString(const DataType& type) {
   switch (type.getKind()) {
      case DataType::Kind::STRING:
         return "STRING";
      case DataType::Kind::DICTIONARY_ENCODED:
         return fmt::format(
            "DICTIONARY_ENCODED({})", dataTypeToString(*cast<DictionaryType>(&type)->getValueType())
         );
      case DataType::Kind::DATE32:
         return "DATE32";
      case DataType::Kind::BOOL:
         return "BOOL";
      case DataType::Kind::INT32:
         return "INT32";
      case DataType::Kind::INT64:
         return "INT64";
      case DataType::Kind::FLOAT:
         return "FLOAT";
      case DataType::Kind::AMINO_ACID_SEQUENCE:
         return fmt::format("AMINO_ACID_SEQUENCE({})", cast<SequenceType>(&type)->getLength());
      case DataType::Kind::NUCLEOTIDE_SEQUENCE:
         return fmt::format("NUCLEOTIDE_SEQUENCE({})", cast<SequenceType>(&type)->getLength());
      case DataType::Kind::ZSTD_COMPRESSED_STRING:
         return "ZSTD_COMPRESSED_STRING";
   }
   RHYDB_UNREACHABLE();
}

std::string_view toSchemaTypeName(DataType::Kind kind) {
   switch (kind) {
      case DataType::Kind::STRING:
         return "string";
      case DataType::Kind::DATE32:
         return "date";
      case DataType::Kind::BOOL:
         return "boolean";
      case DataType::Kind::INT32:
         return "int32";
      case DataType::Kind::INT64:
         return "int64";
      case DataType::Kind::FLOAT:
         return "float";
      case DataType::Kind::NUCLEOTIDE_SEQUENCE:
         return "nucleotideSequence";
      case DataType::Kind::AMINO_ACID_SEQUENCE:
         return "aminoAcidSequence";
      case DataType::Kind::ZSTD_COMPRESSED_STRING:
         return "zstdCompressedString";
      case DataType::Kind::DICTIONARY_ENCODED:
         break;
   }
   RHYDB_UNREACHABLE();
}

std::optional<DataType::Kind> fromSchemaTypeName(std::string_view name) {
   using Kind = DataType::Kind;
   if (name == "int") {
      return Kind::INT32;
   }
   for (const Kind kind : {
           Kind::STRING,
           Kind::DATE32,
           Kind::BOOL,
           Kind::INT32,
           Kind::INT64,
           Kind::FLOAT,
           Kind::NUCLEOTIDE_SEQUENCE,
           Kind::AMINO_ACID_SEQUENCE,
           Kind::ZSTD_COMPRESSED_STRING,
        }) {
      if (toSchemaTypeName(kind) == name) {
         return kind;
      }
   }
   return std::nullopt;
}

}  // namespace rhydb::schema
