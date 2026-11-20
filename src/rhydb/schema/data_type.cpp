#include "rhydb/schema/data_type.h"

#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "rhydb/common/panic.h"

namespace rhydb::schema {

// NOLINTNEXTLINE(misc-no-recursion)
std::string dataTypeToString(const DataType& type) {
   switch (type.getKind()) {
      case DataType::Kind::STRING:
         return "STRING";
      case DataType::Kind::DICTIONARY_ENCODED:
         return fmt::format(
            "DICTIONARY_ENCODED({})", dataTypeToString(*type.as<DictionaryType>().getValueType())
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
         return "AMINO_ACID_SEQUENCE";
      case DataType::Kind::NUCLEOTIDE_SEQUENCE:
         return "NUCLEOTIDE_SEQUENCE";
      case DataType::Kind::ZSTD_COMPRESSED_STRING:
         return "ZSTD_COMPRESSED_STRING";
   }
   RHYDB_UNREACHABLE();
}

std::string_view toSchemaTypeName(const DataType& type) {
   switch (type.getKind()) {
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

std::optional<DataTypePtr> fromSchemaTypeName(std::string_view name) {
   if (name == "int") {
      return DataType::INT32;
   }
   for (const DataTypePtr& type : {
           DataType::STRING,
           DataType::DATE32,
           DataType::BOOL,
           DataType::INT32,
           DataType::INT64,
           DataType::FLOAT,
           DataType::NUCLEOTIDE_SEQUENCE,
           DataType::AMINO_ACID_SEQUENCE,
           DataType::ZSTD_COMPRESSED_STRING,
        }) {
      if (toSchemaTypeName(*type) == name) {
         return type;
      }
   }
   return std::nullopt;
}

DataTypePtr dataTypeFromKinds(DataType::Kind kind, std::optional<DataType::Kind> value_kind) {
   using Kind = DataType::Kind;
   if (kind == Kind::DICTIONARY_ENCODED) {
      if (value_kind == Kind::STRING) {
         return DictionaryType::STRING;
      }
      if (value_kind == Kind::DATE32) {
         return DictionaryType::DATE32;
      }
      throw std::runtime_error("invalid dictionary value type");
   }
   if (value_kind.has_value()) {
      throw std::runtime_error("only dictionary-encoded types have a value type");
   }
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
      case Kind::AMINO_ACID_SEQUENCE:
         return DataType::AMINO_ACID_SEQUENCE;
      case Kind::NUCLEOTIDE_SEQUENCE:
         return DataType::NUCLEOTIDE_SEQUENCE;
      case Kind::ZSTD_COMPRESSED_STRING:
         return DataType::ZSTD_COMPRESSED_STRING;
      case Kind::DICTIONARY_ENCODED:
         break;
   }
   throw std::runtime_error("invalid data type kind");
}

}  // namespace rhydb::schema
