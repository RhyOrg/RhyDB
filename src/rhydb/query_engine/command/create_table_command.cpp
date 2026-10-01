#include "rhydb/query_engine/command/create_table_command.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/database.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/schema/builtin_tables.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::command {

using saneql::FunctionSignature;
using saneql::ParameterDefinition;
using schema::ColumnType;

namespace {

/// One column of a `createTable` statement as written in the query.
struct ColumnDefinition {
   std::string name;
   schema::ColumnType type;
   /// The `name` of the `reference_genomes` row whose sequence is the column's reference sequence
   /// (for aligned sequence columns) or compression dictionary (for unaligned sequence columns).
   /// Unset for value columns.
   std::optional<std::string> reference_name;
};

const std::map<std::string, ColumnType, std::less<>> VALUE_TYPES_WITHOUT_OPTIONS{
   {"int", ColumnType::INT32},
   {"int32", ColumnType::INT32},
   {"int64", ColumnType::INT64},
   {"float", ColumnType::FLOAT},
   {"boolean", ColumnType::BOOL},
   {"date", ColumnType::DATE32},
};

const std::map<std::string, ColumnType, std::less<>> SEQUENCE_TYPES{
   {"nucleotideSequence", ColumnType::NUCLEOTIDE_SEQUENCE},
   {"aminoAcidSequence", ColumnType::AMINO_ACID_SEQUENCE},
   {"unalignedNucleotideSequence", ColumnType::ZSTD_COMPRESSED_STRING},
};

const FunctionSignature STRING_TYPE_SIGNATURE{
   {ParameterDefinition{.name = "generateIndex", .required = false, .positional = false}}
};

const FunctionSignature SEQUENCE_TYPE_SIGNATURE{
   {ParameterDefinition{.name = "reference", .required = true, .positional = false}}
};

const FunctionSignature NO_OPTIONS_SIGNATURE{};

using saneql::ast::Expression;
using saneql::ast::extractBoolLiteral;
using saneql::ast::extractIdentifierName;
using saneql::ast::FunctionCall;
using saneql::ast::Identifier;
using saneql::ast::NamedArgument;
using saneql::ast::PositionalArgument;

// A type is written either as a bare name (`int`) or as a call with named options
// (`string(generateIndex := true)`).
ColumnDefinition parseColumnDefinition(
   const std::string& column_name,
   const Expression& type_expression
) {
   static const std::vector<PositionalArgument> no_positional;
   static const std::vector<NamedArgument> no_named;

   std::string type_name;
   const std::vector<PositionalArgument>* positional = &no_positional;
   const std::vector<NamedArgument>* named = &no_named;
   if (const auto* identifier = std::get_if<Identifier>(&type_expression.value)) {
      type_name = identifier->name;
   } else if (const auto* call = std::get_if<FunctionCall>(&type_expression.value)) {
      type_name = call->function_name;
      positional = &call->positional_arguments;
      named = &call->named_arguments;
   } else {
      throw IllegalQueryException(
         "createTable(): the type of column '{}' must be a type name such as `int`, or a type "
         "with options such as `string(generateIndex := true)`, but got '{}'",
         column_name,
         type_expression.toString()
      );
   }

   if (type_name == "string") {
      auto options = saneql::bindArguments(type_name, STRING_TYPE_SIGNATURE, *positional, *named);
      const auto* generate_index = options.get("generateIndex");
      const bool is_indexed = generate_index != nullptr && extractBoolLiteral(*generate_index);
      return ColumnDefinition{
         .name = column_name,
         .type = is_indexed ? ColumnType::DICTIONARY_ENCODED : ColumnType::STRING,
         .reference_name = std::nullopt
      };
   }

   if (auto value_type = VALUE_TYPES_WITHOUT_OPTIONS.find(type_name);
       value_type != VALUE_TYPES_WITHOUT_OPTIONS.end()) {
      std::ignore = saneql::bindArguments(type_name, NO_OPTIONS_SIGNATURE, *positional, *named);
      return ColumnDefinition{
         .name = column_name, .type = value_type->second, .reference_name = std::nullopt
      };
   }

   if (auto sequence_type = SEQUENCE_TYPES.find(type_name); sequence_type != SEQUENCE_TYPES.end()) {
      auto options = saneql::bindArguments(type_name, SEQUENCE_TYPE_SIGNATURE, *positional, *named);
      return ColumnDefinition{
         .name = column_name,
         .type = sequence_type->second,
         .reference_name = extractIdentifierName(options.at("reference"))
      };
   }

   throw IllegalQueryException(
      "createTable(): unknown type '{}' of column '{}', expected one of string, int, int32, int64, "
      "float, boolean, date, nucleotideSequence, aminoAcidSequence, unalignedNucleotideSequence",
      type_name,
      column_name
   );
}

/// Looks up the sequence of the `reference_genomes` row with the given name and type.
std::optional<std::string> findReferenceSequence(
   const storage::Table& reference_genomes,
   const std::string& name,
   std::string_view type
) {
   const auto& name_column = reference_genomes.columns.string_columns.at("name");
   const auto& type_column = reference_genomes.columns.string_columns.at("type");
   const auto& sequence_column = reference_genomes.columns.string_columns.at("sequence");
   for (const auto row_id : reference_genomes.row_layout) {
      if (name_column.isNull(row_id) || type_column.isNull(row_id) ||
          sequence_column.isNull(row_id)) {
         continue;
      }
      if (name_column.getValueString(row_id) == name &&
          type_column.getValueString(row_id) == type) {
         return sequence_column.getValueString(row_id);
      }
   }
   return std::nullopt;
}

std::string getReferenceSequence(
   const storage::Table& reference_genomes,
   const ColumnDefinition& column,
   std::string_view type
) {
   const auto& reference_name = column.reference_name.value();
   auto sequence = findReferenceSequence(reference_genomes, reference_name, type);
   CHECK_RHYDB_QUERY(
      sequence.has_value(),
      "createTable(): column '{}' requires a reference named '{}' of type '{}', but the table '{}' "
      "does not contain one",
      column.name,
      reference_name,
      type,
      schema::REFERENCE_GENOMES_TABLE_NAME
   );
   return std::move(sequence).value();
}

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> toReferenceSymbols(
   const std::string& sequence,
   const ColumnDefinition& column
) {
   CHECK_RHYDB_QUERY(
      !sequence.empty(),
      "createTable(): the reference '{}' of column '{}' must not be empty",
      column.reference_name.value(),
      column.name
   );
   std::vector<typename SymbolType::Symbol> symbols;
   symbols.reserve(sequence.size());
   for (const char character : sequence) {
      auto symbol = SymbolType::charToSymbol(character);
      CHECK_RHYDB_QUERY(
         symbol.has_value(),
         "createTable(): the reference '{}' of column '{}' contains the illegal {} symbol '{}'",
         column.reference_name.value(),
         column.name,
         SymbolType::SYMBOL_NAME_LOWER_CASE,
         character
      );
      symbols.push_back(*symbol);
   }
   return symbols;
}

std::shared_ptr<storage::column::ColumnMetadata> createColumnMetadata(
   const ColumnDefinition& column,
   const storage::Table& reference_genomes
) {
   switch (column.type) {
      case ColumnType::STRING:
         return std::make_shared<storage::column::StringColumnMetadata>(column.name);
      case ColumnType::DICTIONARY_ENCODED:
         return std::make_shared<storage::column::DictionaryEncodedColumnMetadata>(column.name);
      case ColumnType::DATE32:
      case ColumnType::BOOL:
      case ColumnType::INT32:
      case ColumnType::INT64:
      case ColumnType::FLOAT:
         return std::make_shared<storage::column::ColumnMetadata>(column.name);
      case ColumnType::NUCLEOTIDE_SEQUENCE: {
         auto reference = getReferenceSequence(
            reference_genomes, column, schema::REFERENCE_GENOMES_NUCLEOTIDE_TYPE
         );
         return std::make_shared<storage::column::SequenceColumnMetadata<Nucleotide>>(
            column.name, toReferenceSymbols<Nucleotide>(reference, column)
         );
      }
      case ColumnType::AMINO_ACID_SEQUENCE: {
         auto reference = getReferenceSequence(
            reference_genomes, column, schema::REFERENCE_GENOMES_AMINO_ACID_TYPE
         );
         return std::make_shared<storage::column::SequenceColumnMetadata<AminoAcid>>(
            column.name, toReferenceSymbols<AminoAcid>(reference, column)
         );
      }
      case ColumnType::ZSTD_COMPRESSED_STRING: {
         auto dictionary = getReferenceSequence(
            reference_genomes, column, schema::REFERENCE_GENOMES_NUCLEOTIDE_TYPE
         );
         return std::make_shared<storage::column::ZstdCompressedStringColumnMetadata>(
            column.name, std::move(dictionary)
         );
      }
   }
   RHYDB_UNREACHABLE();
}

using saneql::ast::RecordLiteral;

std::vector<ColumnDefinition> parseColumnDefinitions(const Expression& columns) {
   const auto* record = std::get_if<RecordLiteral>(&columns.value);
   CHECK_RHYDB_QUERY(
      record != nullptr && !record->fields.empty(),
      "createTable(): the columns must be a non-empty record of column types, e.g. "
      "`{{key := string, age := int}}`, but got '{}'",
      columns.toString()
   );

   std::vector<ColumnDefinition> result;
   std::set<std::string> seen_names;
   for (const auto& field : record->fields) {
      CHECK_RHYDB_QUERY(
         seen_names.insert(field.name).second,
         "createTable(): the column '{}' is defined more than once",
         field.name
      );
      result.push_back(parseColumnDefinition(field.name, *field.value));
   }
   return result;
}

/// The primary key must name a column of type `string` without `generateIndex`.
void checkPrimaryKey(
   const std::vector<ColumnDefinition>& columns,
   const std::optional<std::string>& primary_key
) {
   if (!primary_key.has_value()) {
      return;
   }
   auto primary_key_column = std::ranges::find_if(columns, [&](const auto& column) {
      return column.name == *primary_key;
   });
   CHECK_RHYDB_QUERY(
      primary_key_column != columns.end(),
      "createTable(): the primary key '{}' is not one of the table's columns",
      *primary_key
   );
   CHECK_RHYDB_QUERY(
      primary_key_column->type == ColumnType::STRING,
      "createTable(): the primary key '{}' must be a column of type `string` without "
      "`generateIndex`",
      *primary_key
   );
}

}  // namespace

WriteCommandPtr buildCreateTable(
   const saneql::BoundArguments& args,
   const saneql::Tables& tables,
   const saneql::ChildConverter& /*convert_child*/
) {
   auto table_name = extractIdentifierName(args.at("table"));
   auto columns = parseColumnDefinitions(args.at("columns"));
   std::optional<std::string> primary_key;
   if (const auto* primary_key_expr = args.get("primaryKey")) {
      primary_key = extractIdentifierName(*primary_key_expr);
   }
   checkPrimaryKey(columns, primary_key);

   const auto& reference_genomes =
      *tables.at(schema::TableName{std::string{schema::REFERENCE_GENOMES_TABLE_NAME}});
   std::map<schema::ColumnIdentifier, std::shared_ptr<storage::column::ColumnMetadata>>
      column_metadata;
   for (const auto& column : columns) {
      column_metadata.emplace(
         schema::ColumnIdentifier{.name = column.name, .type = column.type},
         createColumnMetadata(column, reference_genomes)
      );
   }
   auto primary_key_identifier = primary_key.transform([](std::string name) {
      return schema::ColumnIdentifier{.name = std::move(name), .type = ColumnType::STRING};
   });

   return std::make_unique<CreateTableCommand>(
      schema::TableName{std::move(table_name)},
      std::make_shared<schema::TableSchema>(
         std::move(column_metadata), std::move(primary_key_identifier)
      )
   );
}

CreateTableCommand::CreateTableCommand(
   schema::TableName table_name,
   std::shared_ptr<schema::TableSchema> table_schema
)
    : table_name_(std::move(table_name)),
      table_schema_(std::move(table_schema)) {}

nlohmann::json CreateTableCommand::execute(
   Database& database,
   const config::QueryOptions& /*query_options*/,
   std::string_view /*request_id*/
) {
   const auto valid_table_name = Database::validateTableName(table_name_.getName());
   CHECK_RHYDB_QUERY(valid_table_name.has_value(), "createTable(): {}", valid_table_name.error());
   CHECK_RHYDB_QUERY(
      !database.tables.contains(table_name_),
      "createTable(): a table named '{}' already exists",
      table_name_.getName()
   );
   database.createTable(table_name_, table_schema_);
   database.updateDataVersion();

   return {{"createdTable", table_name_.getName()}};
}

}  // namespace rhydb::query_engine::command
