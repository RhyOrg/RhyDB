#pragma once

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "rhydb/common/panic.h"

namespace rhydb::schema {

class DataType;

/// Types are shared and immutable, like Arrow's `std::shared_ptr<arrow::DataType>`
using DataTypePtr = std::shared_ptr<const DataType>;

/// The type of a column or value. Like Arrow's data types, every type is an instance of a subclass
/// (e.g. `Int32Type`, `DictionaryType`), and parameterized types hold their parameters in their
/// subclass: a `DictionaryType` holds the type of its dictionary values (see `dictionaryEncoded`),
/// a `SequenceType` the length of its aligned sequences (see `nucleotideSequence`). Use `isA`,
/// `cast` and `dynCast` to get at the subclass of a type.
///
/// Every type exists exactly once (the constants below and the instances the factories return), so
/// comparing two `DataTypePtr`s compares the types. Types are compared structurally via
/// `operator==`/`operator<=>` all the same.
class DataType : public std::enable_shared_from_this<DataType> {
  public:
   /// The discriminator of a type (like clang's `Decl::Kind`), from which `isA` tells the subclass
   /// of a type. Code that handles each type differently switches over `getKind()`.
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
      BINARY,
   };

  private:
   Kind kind;

  protected:
   /// Creates the single instance of a type; the subclasses make `DataType` a friend for this
   template <typename T, typename... Args>
   [[nodiscard]] static DataTypePtr make(Args&&... args) {
      return DataTypePtr{new T(std::forward<Args>(args)...)};
   }

   explicit DataType(Kind kind)
       : kind(kind) {}

  public:
   static const DataTypePtr STRING;
   static const DataTypePtr DATE32;
   static const DataTypePtr BOOL;
   static const DataTypePtr INT32;
   static const DataTypePtr INT64;
   static const DataTypePtr FLOAT;
   static const DataTypePtr ZSTD_COMPRESSED_STRING;
   static const DataTypePtr BINARY;

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

   /// A column of aligned nucleotide sequences of `length` symbols
   [[nodiscard]] static DataTypePtr nucleotideSequence(uint32_t length);

   /// A column of aligned amino acid sequences of `length` symbols
   [[nodiscard]] static DataTypePtr aminoAcidSequence(uint32_t length);

   [[nodiscard]] constexpr Kind getKind() const { return kind; }

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

class SequenceType;

/// Whether `type` is an instance of the subclass `To` (like LLVM's `isa`): of its `KIND`, or of
/// either sequence kind for the abstract `SequenceType`. False for a null `type`.
template <typename To>
[[nodiscard]] bool isA(const DataType* type) {
   if (type == nullptr) {
      return false;
   }
   if constexpr (requires { To::KIND; }) {
      return type->getKind() == To::KIND;
   } else {
      static_assert(std::is_same_v<To, SequenceType>, "a subclass of DataType must have a KIND");
      return To::isSequenceKind(type->getKind());
   }
}

template <typename To>
[[nodiscard]] bool isA(const DataTypePtr& type) {
   return isA<To>(type.get());
}

/// `type` as an instance of the subclass `To`, which it must be (like LLVM's `cast`)
template <typename To>
[[nodiscard]] const To* cast(const DataType* type) {
   RHYDB_ASSERT(isA<To>(type));
   return static_cast<const To*>(type);
}

template <typename To>
[[nodiscard]] const To* cast(const DataTypePtr& type) {
   return cast<To>(type.get());
}

/// `type` as an instance of the subclass `To`, or nullptr if it is none (like LLVM's `dyn_cast`)
template <typename To>
[[nodiscard]] const To* dynCast(const DataType* type) {
   return isA<To>(type) ? static_cast<const To*>(type) : nullptr;
}

template <typename To>
[[nodiscard]] const To* dynCast(const DataTypePtr& type) {
   return dynCast<To>(type.get());
}

/// A column of strings
class StringType final : public DataType {
   friend class DataType;

   StringType()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::STRING;
};

/// A column of dates
class Date32Type final : public DataType {
   friend class DataType;

   Date32Type()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::DATE32;
};

/// A column of booleans
class BoolType final : public DataType {
   friend class DataType;

   BoolType()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::BOOL;
};

/// A column of 32-bit integers
class Int32Type final : public DataType {
   friend class DataType;

   Int32Type()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::INT32;
};

/// A column of 64-bit integers
class Int64Type final : public DataType {
   friend class DataType;

   Int64Type()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::INT64;
};

/// A column of double-precision floating-point numbers
class FloatType final : public DataType {
   friend class DataType;

   FloatType()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::FLOAT;
};

/// A column of strings, each compressed with zstd against a shared dictionary
class ZstdCompressedStringType final : public DataType {
   friend class DataType;

   ZstdCompressedStringType()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::ZSTD_COMPRESSED_STRING;
};

/// A column of arbitrary byte strings
class BinaryType final : public DataType {
   friend class DataType;

   BinaryType()
       : DataType(KIND) {}

  public:
   static constexpr Kind KIND = Kind::BINARY;
};

/// A column storing each distinct value of type `getValueType()` once in a sorted dictionary
class DictionaryType final : public DataType {
   friend class DataType;

   DataTypePtr value_type;

   explicit DictionaryType(DataTypePtr value_type)
       : DataType(KIND),
         value_type(std::move(value_type)) {}

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
      return *value_type <=> *cast<DictionaryType>(&other)->value_type;
   }
};

/// A column of aligned sequences (a `NucleotideSequenceType` or an `AminoAcidSequenceType`) that
/// all have the length of their reference sequence
class SequenceType : public DataType {
   uint32_t length;

  protected:
   SequenceType(Kind kind, uint32_t length)
       : DataType(kind),
         length(length) {}

  public:
   [[nodiscard]] static constexpr bool isSequenceKind(Kind kind) {
      return kind == Kind::NUCLEOTIDE_SEQUENCE || kind == Kind::AMINO_ACID_SEQUENCE;
   }

   /// The type of the aligned sequences of kind `kind` with `length` symbols
   [[nodiscard]] static DataTypePtr get(Kind kind, uint32_t length);

   /// The number of symbols of every sequence
   [[nodiscard]] uint32_t getLength() const { return length; }

  protected:
   [[nodiscard]] std::strong_ordering compareParameters(const DataType& other) const override {
      return length <=> cast<SequenceType>(&other)->length;
   }
};

/// A column of aligned nucleotide sequences
class NucleotideSequenceType final : public SequenceType {
   friend class DataType;

   explicit NucleotideSequenceType(uint32_t length)
       : SequenceType(KIND, length) {}

  public:
   static constexpr Kind KIND = Kind::NUCLEOTIDE_SEQUENCE;
};

/// A column of aligned amino acid sequences
class AminoAcidSequenceType final : public SequenceType {
   friend class DataType;

   explicit AminoAcidSequenceType(uint32_t length)
       : SequenceType(KIND, length) {}

  public:
   static constexpr Kind KIND = Kind::AMINO_ACID_SEQUENCE;
};

// The dictionary types are defined after the plain ones in every translation unit and therefore
// initialized after them
inline const DataTypePtr DataType::STRING = DataType::make<StringType>();
inline const DataTypePtr DataType::DATE32 = DataType::make<Date32Type>();
inline const DataTypePtr DataType::BOOL = DataType::make<BoolType>();
inline const DataTypePtr DataType::INT32 = DataType::make<Int32Type>();
inline const DataTypePtr DataType::INT64 = DataType::make<Int64Type>();
inline const DataTypePtr DataType::FLOAT = DataType::make<FloatType>();
inline const DataTypePtr DataType::ZSTD_COMPRESSED_STRING =
   DataType::make<ZstdCompressedStringType>();
inline const DataTypePtr DataType::BINARY = DataType::make<BinaryType>();
inline const DataTypePtr DictionaryType::STRING = DataType::make<DictionaryType>(DataType::STRING);
inline const DataTypePtr DictionaryType::DATE32 = DataType::make<DictionaryType>(DataType::DATE32);

inline const DataTypePtr& DataType::dictionaryEncoded(const DataType& value_type) {
   RHYDB_ASSERT(canBeDictionaryEncoded(value_type));
   return value_type.kind == Kind::DATE32 ? DictionaryType::DATE32 : DictionaryType::STRING;
}

inline DataTypePtr DataType::nucleotideSequence(uint32_t length) {
   return SequenceType::get(Kind::NUCLEOTIDE_SEQUENCE, length);
}

inline DataTypePtr DataType::aminoAcidSequence(uint32_t length) {
   return SequenceType::get(Kind::AMINO_ACID_SEQUENCE, length);
}

/// The type of the non-parameterized kind `kind` (one of the `DataType` constants), or nullptr for
/// a parameterized or invalid kind
DataTypePtr nonParameterizedDataType(DataType::Kind kind);

/// Writes `type` to a Boost archive: its kind, followed by its parameters: the dictionary value
/// type of a dictionary-encoded type, the length of a sequence type
template <class Archive>
// NOLINTNEXTLINE(misc-no-recursion)
void saveDataType(Archive& archive, const DataType& type) {
   DataType::Kind kind = type.getKind();
   // clang-format off
   archive & kind;
   // clang-format on
   if (const auto* dictionary = dynCast<DictionaryType>(&type)) {
      saveDataType(archive, *dictionary->getValueType());
   } else if (const auto* sequence = dynCast<SequenceType>(&type)) {
      uint32_t length = sequence->getLength();
      // clang-format off
      archive & length;
      // clang-format on
   }
}

/// Reads a type written by `saveDataType`. Throws `std::runtime_error` for an invalid type.
template <class Archive>
// NOLINTNEXTLINE(misc-no-recursion)
DataTypePtr loadDataType(Archive& archive) {
   DataType::Kind kind{};
   // clang-format off
   archive & kind;
   // clang-format on
   if (kind == DataType::Kind::DICTIONARY_ENCODED) {
      const DataTypePtr value_type = loadDataType(archive);
      if (!DataType::canBeDictionaryEncoded(*value_type)) {
         throw std::runtime_error("invalid dictionary value type");
      }
      return DataType::dictionaryEncoded(*value_type);
   }
   if (SequenceType::isSequenceKind(kind)) {
      uint32_t length = 0;
      // clang-format off
      archive & length;
      // clang-format on
      return SequenceType::get(kind, length);
   }
   auto type = nonParameterizedDataType(kind);
   if (type == nullptr) {
      throw std::runtime_error("invalid data type kind");
   }
   return type;
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

/// The name of a type kind in a schema definition (database config, `createTable`), as opposed to
/// the display name `dataTypeToString` of a type. Every kind but `DICTIONARY_ENCODED` has one.
std::string_view toSchemaTypeName(DataType::Kind kind);

/// The kind named `name` in a schema definition (see `toSchemaTypeName`), which also accepts the
/// alias `int`
std::optional<DataType::Kind> fromSchemaTypeName(std::string_view name);

}  // namespace rhydb::schema
