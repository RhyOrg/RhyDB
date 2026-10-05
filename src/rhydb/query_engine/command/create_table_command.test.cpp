#include "rhydb/query_engine/command/create_table_command.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/database.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/schema/builtin_tables.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"
#include "rhydb/test/query_fixture.test.h"

using rhydb::query_engine::IllegalQueryException;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
using rhydb::schema::TableName;
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

namespace {

rhydb::Database makeDatabaseWithReferenceGenomes() {
   rhydb::Database database;
   std::stringstream reference_genomes;
   reference_genomes << R"({"name":"main","type":"nucleotide","sequence":"ACGT"})" << "\n"
                     << R"({"name":"other","type":"nucleotide","sequence":"TTTTT"})" << "\n"
                     << R"({"name":"S","type":"amino_acid","sequence":"MYK*"})" << "\n"
                     << R"({"name":"broken","type":"nucleotide","sequence":"AC?T"})" << "\n";
   database.appendData(
      TableName{std::string{rhydb::schema::REFERENCE_GENOMES_TABLE_NAME}}, reference_genomes
   );
   return database;
}

nlohmann::json executeWrite(rhydb::Database& database, const std::string& statement) {
   return database.executeWrite(statement, rhydb::config::QueryOptions{}, "test_request_id");
}

nlohmann::json executeQuery(rhydb::Database& database, const std::string& query) {
   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      query, database.tables, rhydb::config::QueryOptions{}, "test_request_id"
   );
   return rhydb::test::executeQueryToJsonArray(query_plan);
}

void expectCreateTableError(
   const std::string& statement,
   const std::string& expected_message_part
) {
   auto database = makeDatabaseWithReferenceGenomes();
   EXPECT_THAT(
      [&]() { std::ignore = executeWrite(database, statement); },
      ThrowsMessage<IllegalQueryException>(HasSubstr(expected_message_part))
   );
   EXPECT_FALSE(database.tables.contains(TableName{"t"}));
}

const std::string FULL_SCHEMA_STATEMENT = R"(
createTable(covid, {
   key := string,
   country := string(generateIndex := true),
   age := int,
   big := int64,
   qc := float,
   complete := boolean,
   date := date,
   main := nucleotideSequence(reference := main),
   segment := nucleotideSequence(reference := other),
   "S" := aminoAcidSequence(reference := "S"),
   unaligned_main := zstdCompressedString(
      dictionary := reference_genomes.filter(name = 'main' && type = 'nucleotide').project({sequence})
   )
}, primaryKey := key)
)";

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(CreateTableCommand, createsTableWithFullSchema) {
   auto database = makeDatabaseWithReferenceGenomes();
   const auto version_before = database.getDataVersionTimestamp();

   const auto result = executeWrite(database, FULL_SCHEMA_STATEMENT);

   EXPECT_EQ(result, nlohmann::json({{"createdTable", "covid"}}));
   EXPECT_NE(database.getDataVersionTimestamp(), version_before);
   ASSERT_TRUE(database.tables.contains(TableName{"covid"}));
   ASSERT_TRUE(database.schema.tables.contains(TableName{"covid"}));

   const auto& schema = *database.tables.at(TableName{"covid"})->schema;
   EXPECT_EQ(
      schema.getColumnIdentifiers(),
      (std::vector<ColumnIdentifier>{
         {.name = "S", .type = ColumnType::AMINO_ACID_SEQUENCE},
         {.name = "age", .type = ColumnType::INT32},
         {.name = "big", .type = ColumnType::INT64},
         {.name = "complete", .type = ColumnType::BOOL},
         {.name = "country", .type = ColumnType::DICTIONARY_ENCODED},
         {.name = "date", .type = ColumnType::DATE32},
         {.name = "key", .type = ColumnType::STRING},
         {.name = "main", .type = ColumnType::NUCLEOTIDE_SEQUENCE},
         {.name = "qc", .type = ColumnType::FLOAT},
         {.name = "segment", .type = ColumnType::NUCLEOTIDE_SEQUENCE},
         {.name = "unaligned_main", .type = ColumnType::ZSTD_COMPRESSED_STRING},
      })
   );
   EXPECT_EQ(schema.primary_key, (ColumnIdentifier{.name = "key", .type = ColumnType::STRING}));

   EXPECT_EQ(database.getNucleotideReferenceSequence("covid", "main"), "ACGT");
   EXPECT_EQ(database.getNucleotideReferenceSequence("covid", "segment"), "TTTTT");
   EXPECT_EQ(database.getAminoAcidReferenceSequence("covid", "S"), "MYK*");
   auto unaligned_metadata =
      schema.getColumnMetadata<rhydb::storage::column::ZstdCompressedStringColumn>("unaligned_main"
      );
   ASSERT_TRUE(unaligned_metadata.has_value());
   EXPECT_EQ(unaligned_metadata.value()->dictionary_string, "ACGT");
}

TEST(CreateTableCommand, createdTableAcceptsDataAndIsQueryable) {
   auto database = makeDatabaseWithReferenceGenomes();
   std::ignore = executeWrite(database, FULL_SCHEMA_STATEMENT);

   std::stringstream data;
   data << R"({"key":"a","country":"CH","age":1,"big":10000000000,"qc":0.5,"complete":true,)"
        << R"("date":"2021-03-18","main":{"sequence":"ACGA","insertions":[]},)"
        << R"("segment":{"sequence":"TTTTT","insertions":[]},)"
        << R"("S":{"sequence":"MYK*","insertions":[]},"unaligned_main":"ACGA"})" << "\n"
        << R"({"key":"b","country":"US","age":2,"big":null,"qc":null,"complete":false,)"
        << R"("date":null,"main":null,"segment":null,"S":null,"unaligned_main":null})" << "\n";
   database.appendData(TableName{"covid"}, data);

   EXPECT_EQ(
      executeQuery(database, "covid.filter(country='CH').project({key, age, main})"),
      nlohmann::json::parse(R"([{"key": "a", "age": 1, "main": "ACGA"}])")
   );
}

TEST(CreateTableCommand, createdTableCanBeFilledWithInsertInto) {
   auto database = makeDatabaseWithReferenceGenomes();
   std::ignore = executeWrite(database, "createTable(names, {name := string, type := string})");

   const auto result = executeWrite(
      database,
      "reference_genomes.filter(type='nucleotide').project({name, type}).insertInto(names)"
   );

   EXPECT_EQ(result.at("insertedRows").get<size_t>(), 3);
   EXPECT_FALSE(database.tables.at(TableName{"names"})->schema->primary_key.has_value());
}

TEST(CreateTableCommand, createdTableSurvivesSaveAndLoad) {
   auto database = makeDatabaseWithReferenceGenomes();
   std::ignore = executeWrite(database, FULL_SCHEMA_STATEMENT);

   const auto directory =
      std::filesystem::temp_directory_path() / "rhydb_create_table_command_test";
   std::filesystem::remove_all(directory);
   database.saveDatabaseState(directory);
   auto loaded = rhydb::Database::loadDatabaseStateFromPath(directory);
   std::filesystem::remove_all(directory);

   ASSERT_TRUE(loaded.has_value());
   ASSERT_TRUE(loaded->tables.contains(TableName{"covid"}));
   EXPECT_EQ(
      loaded->tables.at(TableName{"covid"})->schema->getColumnIdentifiers(),
      database.tables.at(TableName{"covid"})->schema->getColumnIdentifiers()
   );
   EXPECT_EQ(loaded->getNucleotideReferenceSequence("covid", "segment"), "TTTTT");
}

TEST(CreateTableCommand, rejectsExistingTable) {
   auto database = makeDatabaseWithReferenceGenomes();
   std::ignore = executeWrite(database, "createTable(t, {key := string})");
   EXPECT_THAT(
      [&]() { std::ignore = executeWrite(database, "createTable(t, {other := int})"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("a table named 't' already exists"))
   );
   EXPECT_THAT(
      [&]() { std::ignore = executeWrite(database, "createTable(reference_genomes, {a := int})"); },
      ThrowsMessage<IllegalQueryException>(
         HasSubstr("a table named 'reference_genomes' already exists")
      )
   );
}

TEST(CreateTableCommand, rejectsInvalidColumnDefinitions) {
   expectCreateTableError("createTable(t, {key, age})", "the columns must be a non-empty record");
   expectCreateTableError("createTable(t, {})", "the columns must be a non-empty record");
   expectCreateTableError("createTable(t)", "createTable() requires argument 'columns'");
   expectCreateTableError(
      "createTable(t, {a := int, a := string})", "the column 'a' is defined more than once"
   );
   expectCreateTableError("createTable(t, {a := varchar})", "unknown type 'varchar' of column 'a'");
   expectCreateTableError("createTable(t, {a := 'int'})", "the type of column 'a' must be");
   expectCreateTableError("createTable('t', {a := int})", "expected identifier");
   expectCreateTableError(
      "createTable(t, {a := string}, primaryKey := 'a')", "expected identifier"
   );
   expectCreateTableError(
      "createTable(t, {a := nucleotideSequence(reference := 'main')})", "expected identifier"
   );
   // The reference is never derived from the column name.
   expectCreateTableError(
      "createTable(t, {main := nucleotideSequence})",
      "nucleotideSequence() requires argument 'reference'"
   );
   expectCreateTableError(
      "createTable(t, {\"S\" := aminoAcidSequence})",
      "aminoAcidSequence() requires argument 'reference'"
   );
   expectCreateTableError(
      "createTable(t, {unaligned_main := zstdCompressedString})",
      "zstdCompressedString() requires argument 'dictionary'"
   );
   expectCreateTableError(
      "createTable(t, {a := unalignedNucleotideSequence(reference := main)})",
      "unknown type 'unalignedNucleotideSequence' of column 'a'"
   );
   expectCreateTableError("createTable(t, {a := int(generateIndex := true)})", "int()");
   expectCreateTableError("createTable(t, {a := string(indexed := true)})", "indexed");
   expectCreateTableError("createTable(t, {a := string(generateIndex := 1)})", "boolean literal");
}

TEST(CreateTableCommand, rejectsInvalidPrimaryKey) {
   expectCreateTableError(
      "createTable(t, {a := string}, primaryKey := b)",
      "the primary key 'b' is not one of the table's columns"
   );
   expectCreateTableError(
      "createTable(t, {a := int}, primaryKey := a)", "the primary key 'a' must be a column of type"
   );
   expectCreateTableError(
      "createTable(t, {a := string(generateIndex := true)}, primaryKey := a)",
      "the primary key 'a' must be a column of type"
   );
}

TEST(CreateTableCommand, rejectsMissingOrInvalidReference) {
   expectCreateTableError(
      "createTable(t, {a := nucleotideSequence(reference := unknown)})",
      "column 'a' requires a reference named 'unknown' of type 'nucleotide'"
   );
   // A reference of the other sequence type does not qualify.
   expectCreateTableError(
      "createTable(t, {a := aminoAcidSequence(reference := main)})",
      "column 'a' requires a reference named 'main' of type 'amino_acid'"
   );
   expectCreateTableError(
      "createTable(t, {a := nucleotideSequence(reference := broken)})",
      "the reference 'broken' of column 'a' contains the illegal nucleotide symbol '?'"
   );
}

TEST(CreateTableCommand, zstdCompressedStringTakesDictionaryFromAnyQuery) {
   auto database = makeDatabaseWithReferenceGenomes();
   std::ignore = executeWrite(
      database,
      "createTable(t, {a := zstdCompressedString(dictionary := "
      "reference_genomes.filter(name = 'S').project({sequence}))})"
   );

   auto metadata =
      database.tables.at(TableName{"t"})
         ->schema->getColumnMetadata<rhydb::storage::column::ZstdCompressedStringColumn>("a");
   ASSERT_TRUE(metadata.has_value());
   EXPECT_EQ(metadata.value()->dictionary_string, "MYK*");
}

TEST(CreateTableCommand, rejectsInvalidDictionary) {
   expectCreateTableError(
      "createTable(t, {a := zstdCompressedString(dictionary := main)})",
      "'main' not found in database"
   );
   expectCreateTableError(
      "createTable(t, {a := zstdCompressedString(dictionary := "
      "reference_genomes.filter(name = 'main'))})",
      "the dictionary of column 'a' must be a query with exactly one column of type `string`"
   );
   expectCreateTableError(
      "createTable(t, {a := zstdCompressedString(dictionary := "
      "reference_genomes.map({n := 1}).project({n}))})",
      "but it has the columns [n: INT64]"
   );
   expectCreateTableError(
      "createTable(t, {a := zstdCompressedString(dictionary := "
      "reference_genomes.filter(name = 'unknown').project({sequence}))})",
      "the dictionary query of column 'a' must produce exactly one row, but produced 0"
   );
   expectCreateTableError(
      "createTable(t, {a := zstdCompressedString(dictionary := "
      "reference_genomes.filter(type = 'nucleotide').project({sequence}))})",
      "the dictionary query of column 'a' must produce exactly one row, but produced 3"
   );
}

// A table's data is saved to `<table>.silo` in the data directory, so a name must neither escape
// that directory nor collide with the database's own metadata files.
TEST(CreateTableCommand, rejectsTableNamesThatAreUnsafeAsFileNames) {
   expectCreateTableError(
      R"(createTable("../escape", {a := int}))",
      "the table name '../escape' may only contain letters, digits, '_' and '-'"
   );
   expectCreateTableError(
      R"(createTable("/tmp/absolute", {a := int}))",
      "the table name '/tmp/absolute' may only contain letters, digits, '_' and '-'"
   );
   expectCreateTableError(
      R"(createTable("with.dot", {a := int}))",
      "the table name 'with.dot' may only contain letters, digits, '_' and '-'"
   );
   expectCreateTableError(R"(createTable("", {a := int}))", "a table name must not be empty");
   expectCreateTableError(
      "createTable(database_schema, {a := int})", "the table name 'database_schema' is reserved"
   );
   expectCreateTableError(
      "createTable(data_version, {a := int})", "the table name 'data_version' is reserved"
   );
}

TEST(CreateTableCommand, databaseRejectsUnsafeTableNamesForEveryCaller) {
   rhydb::Database database;
   EXPECT_THAT(
      [&]() {
         database.createTable(
            TableName{"../escape"}, std::make_shared<rhydb::schema::TableSchema>()
         );
      },
      ThrowsMessage<std::runtime_error>(HasSubstr("Cannot create table: the table name '../escape'")
      )
   );
   EXPECT_FALSE(database.tables.contains(TableName{"../escape"}));
   EXPECT_FALSE(database.schema.tables.contains(TableName{"../escape"}));
}
