#pragma once

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/serialization/access.hpp>
#include <boost/serialization/split_member.hpp>

#include "rhydb/common/panic.h"

namespace rhydb::schema {

/// The type of a column or value, composed like Arrow's data types: every kind but
/// `DICTIONARY_ENCODED` is a complete type on its own, a dictionary-encoded type has the type of
/// its dictionary values as child (see `dictionaryEncoded`).
class DataType {
  public:
   /// The discriminator of a type (like clang's `Decl::Kind`). Code that handles each type
   /// differently switches over `getKind()` and reads the parameters of parameterized kinds from
   /// the `DataType` itself.
   enum class Kind : uint8_t {
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

  private:
   Kind kind = Kind::STRING;
   /// The child type of a parameterized kind (the dictionary values of `DICTIONARY_ENCODED`), or
   /// nullptr.
   std::shared_ptr<const DataType> child;

   DataType(Kind kind, std::shared_ptr<const DataType> child)
       : kind(kind),
         child(std::move(child)) {}

   explicit constexpr DataType(Kind kind)
       : kind(kind) {}

  public:
   static const DataType STRING;
   static const DataType DATE32;
   static const DataType BOOL;
   static const DataType INT32;
   static const DataType INT64;
   static const DataType FLOAT;
   static const DataType AMINO_ACID_SEQUENCE;
   static const DataType NUCLEOTIDE_SEQUENCE;
   static const DataType ZSTD_COMPRESSED_STRING;

   /// Only needed to deserialize into; yields `STRING`
   DataType() = default;

   [[nodiscard]] static constexpr bool canBeDictionaryEncoded(const DataType& value_type) {
      return value_type.kind == Kind::STRING || value_type.kind == Kind::DATE32;
   }

   /// A column storing each distinct value of type `value_type` once in a sorted dictionary
   [[nodiscard]] static DataType dictionaryEncoded(const DataType& value_type) {
      RHYDB_ASSERT(canBeDictionaryEncoded(value_type));
      return DataType{Kind::DICTIONARY_ENCODED, std::make_shared<const DataType>(value_type)};
   }

   [[nodiscard]] constexpr Kind getKind() const { return kind; }

   [[nodiscard]] constexpr bool isDictionaryEncoded() const {
      return kind == Kind::DICTIONARY_ENCODED;
   }

   /// The type of the dictionary values of a `DICTIONARY_ENCODED` type
   [[nodiscard]] DataType getDictionaryValueType() const {
      RHYDB_ASSERT(isDictionaryEncoded());
      return *child;
   }

   /// The type of a single value of this column once read: the dictionary value type for a
   /// dictionary-encoded type, the type itself otherwise
   [[nodiscard]] DataType getPlainType() const { return isDictionaryEncoded() ? *child : *this; }

   // NOLINTNEXTLINE(misc-no-recursion)
   bool operator==(const DataType& other) const {
      if (kind != other.kind || (child == nullptr) != (other.child == nullptr)) {
         return false;
      }
      return child == nullptr || *child == *other.child;
   }

   // NOLINTNEXTLINE(misc-no-recursion)
   std::strong_ordering operator<=>(const DataType& other) const {
      if (const auto ordering = kind <=> other.kind; ordering != 0) {
         return ordering;
      }
      // A type without child sorts first
      if (child == nullptr || other.child == nullptr) {
         if (child == other.child) {
            return std::strong_ordering::equal;
         }
         return child == nullptr ? std::strong_ordering::less : std::strong_ordering::greater;
      }
      return *child <=> *other.child;
   }

  private:
   friend class boost::serialization::access;

   template <class Archive>
   // NOLINTNEXTLINE(misc-no-recursion)
   void save(Archive& archive, const uint32_t version) const {
      // clang-format off
      archive & kind;
      // clang-format on
      if (child != nullptr) {
         child->save(archive, version);
      }
   }

   template <class Archive>
   // NOLINTNEXTLINE(misc-no-recursion)
   void load(Archive& archive, const uint32_t version) {
      // clang-format off
      archive & kind;
      // clang-format on
      child = nullptr;
      if (kind == Kind::DICTIONARY_ENCODED) {
         DataType value_type;
         value_type.load(archive, version);
         child = std::make_shared<const DataType>(std::move(value_type));
      }
   }

   BOOST_SERIALIZATION_SPLIT_MEMBER()
};

inline constinit const DataType DataType::STRING{Kind::STRING};
inline constinit const DataType DataType::DATE32{Kind::DATE32};
inline constinit const DataType DataType::BOOL{Kind::BOOL};
inline constinit const DataType DataType::INT32{Kind::INT32};
inline constinit const DataType DataType::INT64{Kind::INT64};
inline constinit const DataType DataType::FLOAT{Kind::FLOAT};
inline constinit const DataType DataType::AMINO_ACID_SEQUENCE{Kind::AMINO_ACID_SEQUENCE};
inline constinit const DataType DataType::NUCLEOTIDE_SEQUENCE{Kind::NUCLEOTIDE_SEQUENCE};
inline constinit const DataType DataType::ZSTD_COMPRESSED_STRING{Kind::ZSTD_COMPRESSED_STRING};

/// The display name of a type, e.g. in error messages and query plans
std::string dataTypeToString(const DataType& type);

/// The name of a non-parameterized type in a schema definition (database config, `createTable`),
/// as opposed to its display name `dataTypeToString`
std::string_view toSchemaTypeName(const DataType& type);

/// The non-parameterized type named `name` in a schema definition (see `toSchemaTypeName`), which
/// also accepts the alias `int`
std::optional<DataType> fromSchemaTypeName(std::string_view name);

}  // namespace rhydb::schema
