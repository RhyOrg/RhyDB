#include "rhydb/storage/column_group_builder.h"

#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "evobench/evobench.hpp"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/base64.h"
#include "rhydb/common/date32.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/storage/column/column_type_visitor.h"
#include "rhydb/storage/column_group.h"

namespace rhydb::storage {

template <>
std::map<std::string, column::DictionaryEncodedColumn::Builder>& ColumnGroupBuilder::
   getColumnBuilders<column::DictionaryEncodedColumn>() {
   return dictionary_encoded_column_builders;
}

template <>
std::map<std::string, column::StringColumn::Builder>& ColumnGroupBuilder::getColumnBuilders<
   column::StringColumn>() {
   return string_column_builders;
}

template <>
std::map<std::string, column::Int32Column::Builder>& ColumnGroupBuilder::getColumnBuilders<
   column::Int32Column>() {
   return int32_column_builders;
}

template <>
std::map<std::string, column::Int64Column::Builder>& ColumnGroupBuilder::getColumnBuilders<
   column::Int64Column>() {
   return int64_column_builders;
}

template <>
std::map<std::string, column::BoolColumn::Builder>& ColumnGroupBuilder::getColumnBuilders<
   column::BoolColumn>() {
   return bool_column_builders;
}

template <>
std::map<std::string, column::FloatColumn::Builder>& ColumnGroupBuilder::getColumnBuilders<
   column::FloatColumn>() {
   return float_column_builders;
}

template <>
std::map<std::string, column::Date32Column::Builder>& ColumnGroupBuilder::getColumnBuilders<
   column::Date32Column>() {
   return date32_column_builders;
}

template <>
std::map<std::string, column::SequenceColumn<Nucleotide>::Builder>& ColumnGroupBuilder::
   getColumnBuilders<column::SequenceColumn<Nucleotide>>() {
   return nuc_column_builders;
}

template <>
std::map<std::string, column::SequenceColumn<AminoAcid>::Builder>& ColumnGroupBuilder::
   getColumnBuilders<column::SequenceColumn<AminoAcid>>() {
   return aa_column_builders;
}

template <>
std::map<std::string, column::ZstdCompressedStringColumn::Builder>& ColumnGroupBuilder::
   getColumnBuilders<column::ZstdCompressedStringColumn>() {
   return zstd_compressed_string_column_builders;
}

namespace {

#define RAISE_STRING_ERROR_WITH_CONTEXT(error, value, ...)                                  \
   if (error) {                                                                             \
      simdjson::ondemand::raw_json_string line_context;                                     \
      auto error_when_getting_line_context = value.get_raw_json_string().get(line_context); \
      if (error_when_getting_line_context) {                                                \
         return std::unexpected{fmt::format(__VA_ARGS__, simdjson::error_message(error))};  \
      }                                                                                     \
      return std::unexpected{fmt::format(                                                   \
         "{}. Current line: {}",                                                            \
         fmt::format(__VA_ARGS__, simdjson::error_message(error)),                          \
         line_context.raw()                                                                 \
      ),};                                                                                   \
   }

struct InputSequence {
   std::variant<std::string_view, std::string> sequence;

   [[nodiscard]] std::string_view getView() const {
      if (std::holds_alternative<std::string_view>(sequence)) {
         return std::get<std::string_view>(sequence);
      }
      return std::get<std::string>(sequence);
   }
};

template <typename SymbolType>
std::expected<InputSequence, std::string> getSequenceFromJsonLine(
   simdjson::ondemand::value& value,
   std::string_view column_name,
   column::SequenceColumnBuilder<SymbolType>& sequence_builder
) {
   // Determine sequence: try 'sequenceCompressed' (base64-encoded zstd-compressed) first,
   // then fall back to plain 'sequence'.
   InputSequence input_sequence;
   auto compressed_field = value["sequenceCompressed"];
   if (!compressed_field.error()) {
      std::string_view compressed_base64;
      auto error = compressed_field.get(compressed_base64);
      RAISE_STRING_ERROR_WITH_CONTEXT(
         error, value, "error getting field 'sequenceCompressed' in object: {}"
      );
      auto decoded = decodeBase64(compressed_base64);
      if (!decoded.has_value()) {
         return std::unexpected{fmt::format(
            "invalid base64 in 'sequenceCompressed' for column '{}': {}. base64 encoded data "
            "length: {}",
            column_name,
            decoded.error(),
            compressed_base64.size()
         ),};
      }
      try {
         std::string buffer;
         sequence_builder.compressed_input_decompressor.decompress(*decoded, buffer);
         input_sequence.sequence = std::move(buffer);
      } catch (const std::runtime_error& ex) {
         return std::unexpected{
            fmt::format("failed to decompress 'sequenceCompressed': {}", ex.what()),
         };
      }
   } else {
      std::string_view sequence_view;
      auto error = value["sequence"].get(sequence_view);
      RAISE_STRING_ERROR_WITH_CONTEXT(error, value, "error getting field 'sequence' in object: {}");
      input_sequence.sequence = sequence_view;
   }
   return input_sequence;
}

template <typename SymbolType>
std::expected<void, std::string> insertToSequenceBuilder(
   ColumnGroupBuilder& builders,
   const schema::ColumnIdentifier& column,
   simdjson::ondemand::value& value
) {
   auto& sequence_builder =
      builders.getColumnBuilders<column::SequenceColumn<SymbolType>>().at(column.name);
   bool is_null;
   auto error = value.is_null().get(is_null);
   RAISE_STRING_ERROR_WITH_CONTEXT(error, value, "error checking value for null: {}");
   if (is_null) {
      sequence_builder.insertNull();
      return {};
   }
   auto input_sequence = getSequenceFromJsonLine(value, column.name, sequence_builder);
   if (!input_sequence.has_value()) {
      return std::unexpected{input_sequence.error()};
   }
   const std::string_view sequence = input_sequence.value().getView();
   uint32_t offset = 0;
   auto offset_in_file = value["offset"];
   if (!offset_in_file.error()) {
      error = offset_in_file.get<uint32_t>().get(offset);
      RAISE_STRING_ERROR_WITH_CONTEXT(error, value, "error getting field 'offset' in object: {}");
   }
   std::vector<std::string> insertions;
   error = value["insertions"].get(insertions);
   RAISE_STRING_ERROR_WITH_CONTEXT(error, value, "error getting field 'insertions' in object: {}");
   sequence_builder.insert(sequence, offset, insertions);
   return {};
}

class ColumnValueExtractor {
   template <typename T>
   static constexpr std::string_view valueTypeName() {
      if constexpr (std::is_same_v<T, bool>) {
         return "boolean";
      } else if constexpr (std::is_same_v<T, int32_t>) {
         return "int32";
      } else if constexpr (std::is_same_v<T, int64_t>) {
         return "int64";
      } else if constexpr (std::is_same_v<T, double>) {
         return "double";
      } else if constexpr (std::is_same_v<T, std::string_view>) {
         return "string";
      } else {
         static_assert(!std::is_same_v<T, T>, "Unhandled value_type in valueTypeName");
         RHYDB_UNREACHABLE();
      }
   }

  public:
   template <column::Column ColumnType>
   std::expected<void, std::string> operator()(
      ColumnGroupBuilder& builders,
      const schema::ColumnIdentifier& column,
      simdjson::ondemand::value& value
   ) {
      bool is_null;
      auto error = value.is_null().get(is_null);
      RAISE_STRING_ERROR_WITH_CONTEXT(error, value, "error checking value for null: {}");
      if (is_null) {
         builders.getColumnBuilders<ColumnType>().at(column.name).insertNull();
      } else {
         typename ColumnType::value_type column_value;
         error = value.get(column_value);
         RAISE_STRING_ERROR_WITH_CONTEXT(
            error,
            value,
            "error getting value as {}: {}. {}",
            valueTypeName<typename ColumnType::value_type>(),
            value.raw_json_token()
         );
         builders.getColumnBuilders<ColumnType>().at(column.name).insert(column_value);
      }
      return {};
   }
};

template <>
std::expected<void, std::string> ColumnValueExtractor::operator()<column::Date32Column>(
   ColumnGroupBuilder& builders,
   const schema::ColumnIdentifier& column,
   simdjson::ondemand::value& value
) {
   bool is_null;
   auto error = value.is_null().get(is_null);
   RAISE_STRING_ERROR_WITH_CONTEXT(error, value, "error checking value for null: {}");
   if (is_null) {
      builders.getColumnBuilders<column::Date32Column>().at(column.name).insertNull();
   } else {
      std::string_view column_value;
      error = value.get(column_value);
      RAISE_STRING_ERROR_WITH_CONTEXT(
         error, value, "error getting value as string: {}. {}", value.raw_json_token()
      );
      auto date_result = rhydb::common::stringToDate32(column_value);
      if (!date_result.has_value()) {
         return std::unexpected{date_result.error()};
      }
      builders.getColumnBuilders<column::Date32Column>()
         .at(column.name)
         .insert(date_result.value());
   }
   return {};
}

template <>
std::expected<void, std::string> ColumnValueExtractor::operator(
)<column::SequenceColumn<AminoAcid>>(
   ColumnGroupBuilder& builders,
   const schema::ColumnIdentifier& column,
   simdjson::ondemand::value& value
) {
   return insertToSequenceBuilder<AminoAcid>(builders, column, value);
}

template <>
std::expected<void, std::string> ColumnValueExtractor::operator(
)<column::SequenceColumn<Nucleotide>>(
   ColumnGroupBuilder& builders,
   const schema::ColumnIdentifier& column,
   simdjson::ondemand::value& value
) {
   return insertToSequenceBuilder<Nucleotide>(builders, column, value);
}

}  // namespace

ColumnGroupBuilder::ColumnGroupBuilder(
   const schema::TableSchema& schema,
   const ColumnGroup& columns
) {
   auto builder_initializer = [this, &columns]<column::Column ColumnType>(
                                 const schema::ColumnIdentifier& column_identifier,
                                 const schema::TableSchema& table_schema
                              ) {
      auto* column_metadata =
         table_schema.getColumnMetadata<ColumnType>(column_identifier.name).value();
      metadata.emplace_back(column_identifier);
      if constexpr (std::is_same_v<ColumnType, column::SequenceColumn<Nucleotide>> ||
                    std::is_same_v<ColumnType, column::SequenceColumn<AminoAcid>>) {
         // Seed the sequence builder with the column's current (possibly adapted)
         // local reference so newly buffered rows share the stored reference basis.
         const auto& column = columns.getColumns<ColumnType>().at(column_identifier.name);
         getColumnBuilders<ColumnType>().emplace(
            column_identifier.name,
            typename ColumnType::Builder{column_metadata, column.local_reference_sequence_string}
         );
      } else if constexpr (std::is_constructible_v<
                              typename ColumnType::Builder,
                              typename ColumnType::Metadata*>) {
         getColumnBuilders<ColumnType>().emplace(
            column_identifier.name, typename ColumnType::Builder{column_metadata}
         );
      } else {
         getColumnBuilders<ColumnType>().emplace(
            column_identifier.name, typename ColumnType::Builder{}
         );
      },
   };
   for (const auto& col : schema.getColumnIdentifiers()) {
      column::visit(col.type, builder_initializer, col, schema);
   }
}

namespace {
class NumValuesVisitor {
  public:
   template <column::Column ColumnType>
   size_t operator()(ColumnGroupBuilder& builders, const std::string& name) {
      return builders.getColumnBuilders<ColumnType>().at(name).numValues();
   }
};
}  // namespace

size_t ColumnGroupBuilder::numBufferedRows() const {
   if (metadata.empty()) {
      return 0;
   }
   const auto& [name, type] = metadata.front();
   return column::visit(type, NumValuesVisitor{}, const_cast<ColumnGroupBuilder&>(*this), name);
}

std::expected<void, std::string> ColumnGroupBuilder::addJsonValueToColumn(
   const schema::ColumnIdentifier& column,
   simdjson::ondemand::value& value
) {
   EVOBENCH_SCOPE_EVERY(1000, "ColumnGroupBuilder", "addJsonValueToColumn");
   auto success = column::visit(column.type, ColumnValueExtractor{}, *this, column, value);
   if (!success.has_value()) {
      return std::unexpected(
         fmt::format("error inserting into column '{}': {}", column.name, success.error())
      );
   }
   return success;
}

namespace {
class MoveRowVisitor {
  public:
   template <column::Column ColumnType>
   void operator()(
      ColumnGroupBuilder& source,
      ColumnGroupBuilder& destination,
      const std::string& name,
      size_t index
   ) {
      source.getColumnBuilders<ColumnType>().at(name).moveRowTo(
         index, destination.getColumnBuilders<ColumnType>().at(name)
      );
   }
};

class ClearVisitor {
  public:
   template <column::Column ColumnType>
   void operator()(ColumnGroupBuilder& builders, const std::string& name) {
      // finalize() moves the buffer out and clears it; discarding the result drops the rows.
      static_cast<void>(builders.getColumnBuilders<ColumnType>().at(name).finalize());
   }
};
}  // namespace

void ColumnGroupBuilder::moveRowTo(size_t index, ColumnGroupBuilder& destination) {
   for (const auto& column : metadata) {
      column::visit(column.type, MoveRowVisitor{}, *this, destination, column.name, index);
   }
}

void ColumnGroupBuilder::clear() {
   for (const auto& column : metadata) {
      column::visit(column.type, ClearVisitor{}, *this, column.name);
   }
}

std::optional<std::pair<uint32_t, uint32_t>> ColumnGroupBuilder::coverageRangeAt(
   const schema::ColumnIdentifier& sequence_column,
   size_t index
) const {
   if (sequence_column.type == schema::ColumnType::NUCLEOTIDE_SEQUENCE) {
      return nuc_column_builders.at(sequence_column.name).coverageRangeAt(index);
   }
   return aa_column_builders.at(sequence_column.name).coverageRangeAt(index);
}

}  // namespace rhydb::storage
