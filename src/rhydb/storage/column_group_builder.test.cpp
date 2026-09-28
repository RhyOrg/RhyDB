#include "rhydb/storage/column_group_builder.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <simdjson.h>
#include <simdutf.h>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/bool_column.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/float_column.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"
#include "rhydb/zstd/zstd_compressor.h"
#include "rhydb/zstd/zstd_dictionary.h"

using rhydb::Nucleotide;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::TableSchema;
using rhydb::storage::ColumnGroupBuilder;
using rhydb::storage::column::BoolColumn;
using rhydb::storage::column::Column;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::Date32Column;
using rhydb::storage::column::FloatColumn;
using rhydb::storage::column::Int32Column;
using rhydb::storage::column::SequenceColumn;
using rhydb::storage::column::SequenceColumnMetadata;
using rhydb::storage::column::StringColumn;
using rhydb::storage::column::StringColumnMetadata;

namespace {

// Builds a single-column TableSchema (the column also serves as primary key, as
// the schema only requires the primary key to be present in the metadata map).
TableSchema makeSingleColumnSchema(
   const ColumnIdentifier& column_id,
   std::shared_ptr<ColumnMetadata> metadata
) {
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata;
   column_metadata.emplace(column_id, std::move(metadata));
   return TableSchema{std::move(column_metadata), column_id};
}

template <Column ColumnType>
std::expected<void, std::string> setupColumnAndInsertJson(
   const std::string& column_name,
   const std::string& json_string
) {
   std::shared_ptr<typename ColumnType::Metadata> meta;
   if constexpr (std::is_same_v<ColumnType, SequenceColumn<Nucleotide>>) {
      meta = std::make_shared<SequenceColumnMetadata<Nucleotide>>(
         column_name, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
      );
   } else {
      meta = std::make_shared<typename ColumnType::Metadata>(column_name);
   }
   const ColumnIdentifier column_id{column_name, ColumnType::TYPE};
   auto schema = std::make_shared<TableSchema>(makeSingleColumnSchema(column_id, meta));
   const rhydb::storage::Table table{rhydb::schema::TableName::getDefault(), schema};
   ColumnGroupBuilder builder{*schema, table.columns};

   simdjson::ondemand::parser parser;
   const simdjson::padded_string json(json_string);
   auto doc = parser.iterate(json).value_unsafe();
   simdjson::ondemand::value val = doc[column_name].value_unsafe();

   return builder.addJsonValueToColumn(column_id, val);
}

std::expected<void, std::string> setupNucleotideColumnAndInsertJson(
   const std::string& column_name,
   const std::vector<Nucleotide::Symbol>& reference,
   const std::string& json_string
) {
   auto meta = std::make_shared<SequenceColumnMetadata<Nucleotide>>(
      column_name, std::vector<Nucleotide::Symbol>{reference}
   );
   const ColumnIdentifier column_id{.name = column_name, .type = SequenceColumn<Nucleotide>::TYPE};
   auto schema = std::make_shared<TableSchema>(makeSingleColumnSchema(column_id, meta));
   const rhydb::storage::Table table{rhydb::schema::TableName::getDefault(), schema};
   ColumnGroupBuilder builder{*schema, table.columns};

   simdjson::ondemand::parser parser;
   const simdjson::padded_string json(json_string);
   auto doc = parser.iterate(json).value_unsafe();
   simdjson::ondemand::value val = doc[column_name].value_unsafe();

   return builder.addJsonValueToColumn(column_id, val);
}

std::string compressAndBase64Encode(std::string_view sequence, const std::string& reference) {
   auto cdict = std::make_shared<rhydb::ZstdCDictionary>(reference, 3);
   rhydb::ZstdCompressor compressor(cdict);
   const auto compressed = compressor.compress(sequence.data(), sequence.size());

   const size_t base64_len = simdutf::base64_length_from_binary(compressed.size());
   std::string encoded(base64_len, '\0');
   simdutf::binary_to_base64(compressed.data(), compressed.size(), encoded.data());
   return encoded;
}

}  // namespace

TEST(ColumnGroupBuilder, givenIntegerValueForBoolColumn_returnsColumnInsertError) {
   const auto result = setupColumnAndInsertJson<BoolColumn>("bool_col", R"({"bool_col": 42})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'bool_col': error getting value as boolean: 42."
      )
   );
}

TEST(ColumnGroupBuilder, givenStringValueForIntColumn_returnsColumnInsertError) {
   const auto result = setupColumnAndInsertJson<Int32Column>("int_col", R"({"int_col": "hello"})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'int_col': error getting value as int32: \"hello\"."
      )
   );
}

TEST(ColumnGroupBuilder, givenStringValueForFloatColumn_returnsColumnInsertError) {
   const auto result =
      setupColumnAndInsertJson<FloatColumn>("float_col", R"({"float_col": "hello"})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'float_col': error getting value as double: \"hello\"."
      )
   );
}

TEST(ColumnGroupBuilder, givenIntegerValueForStringColumn_returnsColumnInsertError) {
   const auto result =
      setupColumnAndInsertJson<StringColumn>("string_col", R"({"string_col": 42})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'string_col': error getting value as string: 42."
      )
   );
}

TEST(ColumnGroupBuilder, givenIntegerValueForDate32Column_returnsColumnInsertError) {
   const auto result = setupColumnAndInsertJson<Date32Column>("date_col", R"({"date_col": 42})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'date_col': error getting value as string: 42."
      )
   );
}

TEST(ColumnGroupBuilder, givenInvalidDateStringForDate32Column_returnsColumnInsertError) {
   const auto result =
      setupColumnAndInsertJson<Date32Column>("date_col", R"({"date_col": "not-a-date"})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'date_col': Invalid date format 'not-a-date': expected "
         "exactly YYYY-MM-DD"
      )
   );
}

TEST(ColumnGroupBuilder, givenOutOfRangeDateForDate32Column_returnsColumnInsertError) {
   const auto result =
      setupColumnAndInsertJson<Date32Column>("date_col", R"({"date_col": "2021-13-01"})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'date_col': Invalid calendar date '2021-13-01'"
      )
   );
}

TEST(ColumnGroupBuilder, givenObjectMissingSequenceField_returnsColumnInsertError) {
   const auto result =
      setupColumnAndInsertJson<SequenceColumn<Nucleotide>>("nuc_col", R"({"nuc_col": {}})");

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'nuc_col': error getting field 'sequence' in object:"
      )
   );
}

TEST(ColumnGroupBuilder, givenObjectMissingInsertionsField_returnsColumnInsertError) {
   const auto result = setupColumnAndInsertJson<SequenceColumn<Nucleotide>>(
      "nuc_col", R"({"nuc_col": {"sequence": "A"}})"
   );

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(
      result.error(),
      testing::HasSubstr(
         "error inserting into column 'nuc_col': error getting field 'insertions' in object:"
      )
   );
}

TEST(ColumnGroupBuilder, givenValidSequenceCompressed_succeeds) {
   const std::vector<Nucleotide::Symbol> reference = {
      Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T,
   };
   const std::string reference_str = "ACGT";
   const std::string encoded = compressAndBase64Encode("ACGT", reference_str);

   const auto result = setupNucleotideColumnAndInsertJson(
      "nuc_col",
      reference,
      fmt::format(R"({{"nuc_col": {{"sequenceCompressed": "{}", "insertions": []}}}})", encoded)
   );

   ASSERT_TRUE(result.has_value());
}

TEST(ColumnGroupBuilder, givenSequenceCompressedWithMutation_succeeds) {
   const std::vector<Nucleotide::Symbol> reference = {
      Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T,
   };
   const std::string reference_str = "ACGT";
   // Sequence differs from reference at position 1 (C → T)
   const std::string encoded = compressAndBase64Encode("ATGT", reference_str);

   const auto result = setupNucleotideColumnAndInsertJson(
      "nuc_col",
      reference,
      fmt::format(R"({{"nuc_col": {{"sequenceCompressed": "{}", "insertions": []}}}})", encoded)
   );

   ASSERT_TRUE(result.has_value());
}

TEST(ColumnGroupBuilder, givenSequenceCompressedMultipleRows_succeeds) {
   std::vector<Nucleotide::Symbol> reference = {
      Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T,
   };
   const std::string reference_str = "ACGT";

   auto meta =
      std::make_shared<SequenceColumnMetadata<Nucleotide>>("nuc_col", std::move(reference));
   const ColumnIdentifier column_id{.name = "nuc_col", .type = SequenceColumn<Nucleotide>::TYPE};
   auto schema = std::make_shared<TableSchema>(makeSingleColumnSchema(column_id, meta));
   const rhydb::storage::Table table{rhydb::schema::TableName::getDefault(), schema};
   ColumnGroupBuilder builder{*schema, table.columns};

   for (const std::string_view sequence : {"ACGT", "ATGT", "ACGT"}) {
      const std::string encoded = compressAndBase64Encode(sequence, reference_str);
      const std::string json =
         fmt::format(R"({{"nuc_col": {{"sequenceCompressed": "{}", "insertions": []}}}})", encoded);

      simdjson::ondemand::parser parser;
      const simdjson::padded_string padded(json);
      auto doc = parser.iterate(padded).value_unsafe();
      simdjson::ondemand::value val = doc["nuc_col"].value_unsafe();

      const auto result = builder.addJsonValueToColumn(column_id, val);
      ASSERT_TRUE(result.has_value());
   }
}

TEST(ColumnGroupBuilder, givenSequenceCompressedWithInvalidBase64_returnsError) {
   const std::vector<Nucleotide::Symbol> reference = {Nucleotide::Symbol::A};

   const auto result = setupNucleotideColumnAndInsertJson(
      "nuc_col",
      reference,
      R"({"nuc_col": {"sequenceCompressed": "not!valid@base64#", "insertions": []}})"
   );

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(result.error(), testing::HasSubstr("invalid base64"));
}

TEST(ColumnGroupBuilder, givenSequenceCompressedWithInvalidZstdData_returnsError) {
   const std::vector<Nucleotide::Symbol> reference = {
      Nucleotide::Symbol::A, Nucleotide::Symbol::C, Nucleotide::Symbol::G, Nucleotide::Symbol::T,
   };
   // Valid base64 but the decoded bytes are not valid zstd-compressed data
   const std::string random_bytes = "\x01\x02\x03\x04\x05\x06\x07\x08";
   const size_t base64_len = simdutf::base64_length_from_binary(random_bytes.size());
   std::string encoded(base64_len, '\0');
   simdutf::binary_to_base64(random_bytes.data(), random_bytes.size(), encoded.data());

   const auto result = setupNucleotideColumnAndInsertJson(
      "nuc_col",
      reference,
      fmt::format(R"({{"nuc_col": {{"sequenceCompressed": "{}", "insertions": []}}}})", encoded)
   );

   ASSERT_FALSE(result.has_value());
   EXPECT_THAT(result.error(), testing::HasSubstr("failed to decompress"));
}
