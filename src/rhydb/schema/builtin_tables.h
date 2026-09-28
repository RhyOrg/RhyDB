#pragma once

#include <map>
#include <memory>
#include <string_view>

#include "rhydb/schema/database_schema.h"

namespace rhydb::schema {

/// The name of the built-in table holding the reference genomes, one row per nucleotide and amino
/// acid sequence.
inline constexpr std::string_view REFERENCE_GENOMES_TABLE_NAME = "reference_genomes";

/// The values of the `type` column of the reference genomes table.
inline constexpr std::string_view REFERENCE_GENOMES_NUCLEOTIDE_TYPE = "nucleotide";
inline constexpr std::string_view REFERENCE_GENOMES_AMINO_ACID_TYPE = "amino_acid";

/// Built-in tables are ordinary tables (persisted, queryable and writable like any other) that
/// every database is guaranteed to contain. They are created empty together with the database, and
/// are added to databases loaded from a state that predates them.
[[nodiscard]] std::map<TableName, std::shared_ptr<TableSchema>> getBuiltinTableSchemas();

}  // namespace rhydb::schema
