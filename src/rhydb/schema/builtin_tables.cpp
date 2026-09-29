#include "rhydb/schema/builtin_tables.h"

#include <string>

#include "rhydb/storage/column/string_column.h"

namespace rhydb::schema {

namespace {

std::shared_ptr<TableSchema> createReferenceGenomesTableSchema() {
   // No primary key: a nucleotide and an amino acid sequence may share a name, so only (name, type)
   // identifies a row.
   const ColumnIdentifier name_column{.name = "name", .type = ColumnType::STRING};
   // Either "nucleotide" or "amino_acid".
   const ColumnIdentifier type_column{.name = "type", .type = ColumnType::STRING};
   const ColumnIdentifier sequence_column{.name = "sequence", .type = ColumnType::STRING};
   auto table_schema = std::make_shared<TableSchema>();
   for (const auto& column : {name_column, type_column, sequence_column}) {
      table_schema->column_metadata.emplace(
         column, std::make_shared<storage::column::StringColumnMetadata>(column.name)
      );
   }
   return table_schema;
}

}  // namespace

std::map<TableName, std::shared_ptr<TableSchema>> getBuiltinTableSchemas() {
   std::map<TableName, std::shared_ptr<TableSchema>> result;
   result.emplace(
      TableName{std::string{REFERENCE_GENOMES_TABLE_NAME}}, createReferenceGenomesTableSchema()
   );
   return result;
}

}  // namespace rhydb::schema
