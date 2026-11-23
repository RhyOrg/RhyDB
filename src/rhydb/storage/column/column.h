#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "rhydb/schema/data_type.h"

namespace rhydb::storage::column {

/// During ingestion, data is buffered into fixed-size chunks of at most this
/// many rows. A column builder accumulates one such chunk and the resulting
/// value array is applied to the column in a single bulk insert.
static constexpr size_t COLUMN_CHUNK_SIZE = 1UL << 16;

template <typename T>
concept Column = requires(T column) {
   typename T::Metadata;
   // ensure it is actually a type
   requires std::is_class_v<typename T::Metadata> ||
               std::is_same_v<typename T::Metadata, typename T::Metadata>;

   requires std::is_constructible_v<T, typename T::Metadata*>;

   // The type of a single value as it is handed to the column during ingestion: the NDJSON reader
   // turns a non-null JSON value into a `value_type` and passes it to `T::Builder::insert`. It
   // may be a non-owning view (e.g. `std::string_view`), so the builder copies what it keeps.
   // `void` for columns that are not ingested value by value (e.g. sequences).
   typename T::value_type;

   // A column stores its values as the immutable chunks in which they were ingested. Iterating the
   // rows themselves is the table's job (it owns the shared `RowLayout`); a column only reports how
   // many chunks it holds so the table can check every column was appended to in lockstep.
   { column.numChunks() } -> std::convertible_to<std::size_t>;

   { column.chunkSize(static_cast<uint32_t>(0)) } -> std::convertible_to<uint32_t>;

   // The kind of the column's type, as a compile-time constant to dispatch on with `if constexpr`
   requires std::same_as<decltype(T::KIND), const schema::DataType::Kind>;

   // The full type of this column. A column class storing a single type has it as static `TYPE`,
   // but columns whose type depends on their metadata (e.g. the length of a sequence) report it
   // per instance.
   { column.type() } -> std::convertible_to<schema::DataTypePtr>;
};

/// Whether `type` is stored by the column class `T`: its `TYPE`, or any type of its `KIND` if the
/// type depends on the column's metadata
template <Column T>
[[nodiscard]] bool storesType(const schema::DataType& type) {
   if constexpr (requires { T::TYPE; }) {
      return *T::TYPE == type;
   } else {
      return type.getKind() == T::KIND;
   }
}

}  // namespace rhydb::storage::column
