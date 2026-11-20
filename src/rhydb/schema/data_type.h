#pragma once

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "rhydb/common/panic.h"

namespace rhydb::schema {

class DataType;

/// Types are shared and immutable, like Arrow's `std::shared_ptr<arrow::DataType>`
using DataTypePtr = std::shared_ptr<const DataType>;

/// The type of a column or value, composed like Arrow's data types: every kind but
/// `DICTIONARY_ENCODED` is a complete type on its own, a dictionary-encoded type is a
/// `DictionaryType` holding the type of its dictionary values (see `dictionaryEncoded`).
///
/// Every type exists exactly once (the constants below and the instances `dictionaryEncoded`
/// returns), so comparing two `DataTypePtr`s compares the types. Types are compared structurally
/// via `operator==`/`operator<=>` all the same.
class DataType : public std::enable_shared_from_this<DataType> {
  public:
   /// The discriminator of a type (like clang's `Decl::Kind`). Code that handles each type
   /// differently switches over `getKind()` and reads the parameters of parameterized kinds from
   /// the subclass (see `as`).
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
   Kind kind;

   explicit DataType(Kind kind)
       : kind(kind) {}

   [[nodiscard]] static DataTypePtr make(Kind kind) { return DataTypePtr{new DataType(kind)}; }

   friend class DictionaryType;

  public:
   static const DataTypePtr STRING;
   static const DataTypePtr DATE32;
   static const DataTypePtr BOOL;
   static const DataTypePtr INT32;
   static const DataTypePtr INT64;
   static const DataTypePtr FLOAT;
   static const DataTypePtr AMINO_ACID_SEQUENCE;
   static const DataTypePtr NUCLEOTIDE_SEQUENCE;
   static const DataTypePtr ZSTD_COMPRESSED_STRING;

   DataType(const DataType&) = delete;
   DataType& operator=(const DataType&) = delete;
   DataType(DataType&&) = delete;
   DataType& operator=(DataType&&) = delete;
   virtual ~DataType() = default;

   [[nodiscard]] static constexpr bool canBeDictionaryEncoded(const DataType& value_type) {
      return value_type.kind == Kind::STRING || value_type.kind == Kind::DATE32;
   }

   /// A column storing each distinct value of type `value_type` once in a sorted dictionary
   [[nodiscard]] static const DataTypePtr& dictionaryEncoded(const DataType& value_type);

   [[nodiscard]] constexpr Kind getKind() const { return kind; }

   [[nodiscard]] constexpr bool isDictionaryEncoded() const {
      return kind == Kind::DICTIONARY_ENCODED;
   }

   /// This type as its subclass `T`, which must be the class of its kind
   template <typename T>
   [[nodiscard]] const T& as() const {
      RHYDB_ASSERT(kind == T::KIND);
      return static_cast<const T&>(*this);
   }

   /// The type of a single value of this column once read: the dictionary value type for a
   /// dictionary-encoded type, the type itself otherwise
   [[nodiscard]] virtual DataTypePtr getPlainType() const { return shared_from_this(); }

   bool operator==(const DataType& other) const { return (*this <=> other) == 0; }

   std::strong_ordering operator<=>(const DataType& other) const {
      if (const auto ordering = kind <=> other.kind; ordering != 0) {
         return ordering;
      }
      return compareParameters(other);
   }

  protected:
   /// Orders this type against `other` of the same kind by their parameters
   [[nodiscard]] virtual std::strong_ordering compareParameters(const DataType& /*other*/) const {
      return std::strong_ordering::equal;
   }
};

/// A column storing each distinct value of type `getValueType()` once in a sorted dictionary
class DictionaryType final : public DataType {
   DataTypePtr value_type;

   explicit DictionaryType(DataTypePtr value_type)
       : DataType(Kind::DICTIONARY_ENCODED),
         value_type(std::move(value_type)) {}

   [[nodiscard]] static DataTypePtr make(DataTypePtr value_type) {
      return DataTypePtr{new DictionaryType(std::move(value_type))};
   }

  public:
   static constexpr Kind KIND = Kind::DICTIONARY_ENCODED;

   /// The dictionary-encoded types, one for each type that `canBeDictionaryEncoded`
   static const DataTypePtr STRING;
   static const DataTypePtr DATE32;

   /// The type of the dictionary values
   [[nodiscard]] const DataTypePtr& getValueType() const { return value_type; }

   [[nodiscard]] DataTypePtr getPlainType() const override { return value_type; }

  protected:
   [[nodiscard]] std::strong_ordering compareParameters(const DataType& other) const override {
      return *value_type <=> *other.as<DictionaryType>().value_type;
   }
};

// The dictionary types are defined after the plain ones in every translation unit and therefore
// initialized after them
inline const DataTypePtr DataType::STRING = DataType::make(Kind::STRING);
inline const DataTypePtr DataType::DATE32 = DataType::make(Kind::DATE32);
inline const DataTypePtr DataType::BOOL = DataType::make(Kind::BOOL);
inline const DataTypePtr DataType::INT32 = DataType::make(Kind::INT32);
inline const DataTypePtr DataType::INT64 = DataType::make(Kind::INT64);
inline const DataTypePtr DataType::FLOAT = DataType::make(Kind::FLOAT);
inline const DataTypePtr DataType::AMINO_ACID_SEQUENCE = DataType::make(Kind::AMINO_ACID_SEQUENCE);
inline const DataTypePtr DataType::NUCLEOTIDE_SEQUENCE = DataType::make(Kind::NUCLEOTIDE_SEQUENCE);
inline const DataTypePtr DataType::ZSTD_COMPRESSED_STRING =
   DataType::make(Kind::ZSTD_COMPRESSED_STRING);
inline const DataTypePtr DictionaryType::STRING = DictionaryType::make(DataType::STRING);
inline const DataTypePtr DictionaryType::DATE32 = DictionaryType::make(DataType::DATE32);

inline const DataTypePtr& DataType::dictionaryEncoded(const DataType& value_type) {
   RHYDB_ASSERT(canBeDictionaryEncoded(value_type));
   return value_type.kind == Kind::DATE32 ? DictionaryType::DATE32 : DictionaryType::STRING;
}

/// The type with the given kind and, for a dictionary-encoded kind, the dictionary value kind.
/// Throws `std::runtime_error` if there is no such type.
DataTypePtr dataTypeFromKinds(DataType::Kind kind, std::optional<DataType::Kind> value_kind);

/// Writes `type` to a Boost archive: its kind, followed by the kind of the dictionary values for a
/// dictionary-encoded type
template <class Archive>
void saveDataType(Archive& archive, const DataType& type) {
   DataType::Kind kind = type.getKind();
   // clang-format off
   archive & kind;
   // clang-format on
   if (type.isDictionaryEncoded()) {
      saveDataType(archive, *type.as<DictionaryType>().getValueType());
   }
}

/// Reads a type written by `saveDataType`
template <class Archive>
DataTypePtr loadDataType(Archive& archive) {
   DataType::Kind kind{};
   // clang-format off
   archive & kind;
   // clang-format on
   if (kind != DataType::Kind::DICTIONARY_ENCODED) {
      return dataTypeFromKinds(kind, std::nullopt);
   }
   DataType::Kind value_kind{};
   // clang-format off
   archive & value_kind;
   // clang-format on
   return dataTypeFromKinds(kind, value_kind);
}

/// Orders `DataTypePtr`s by their types, e.g. as key of a `std::map`
struct DataTypePtrLess {
   bool operator()(const DataTypePtr& left, const DataTypePtr& right) const {
      return *left < *right;
   }
};

/// The display name of a type, e.g. in error messages and query plans
std::string dataTypeToString(const DataType& type);

inline std::string dataTypeToString(const DataTypePtr& type) {
   return dataTypeToString(*type);
}

/// The name of a non-parameterized type in a schema definition (database config, `createTable`),
/// as opposed to its display name `dataTypeToString`
std::string_view toSchemaTypeName(const DataType& type);

/// The non-parameterized type named `name` in a schema definition (see `toSchemaTypeName`), which
/// also accepts the alias `int`
std::optional<DataTypePtr> fromSchemaTypeName(std::string_view name);

}  // namespace rhydb::schema
