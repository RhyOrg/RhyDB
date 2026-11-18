#pragma once

#include <filesystem>
#include <optional>

#include "rhydb/config/preprocessing_config.h"

#include "rhydb/database.h"

namespace rhydb::preprocessing {

Database preprocessing(const config::PreprocessingConfig& preprocessing_config);

/// Creates the table `table_name` in `database` from `initialization_files`, appends the NDJSON
/// data of `input_file` (stdin if not given) to it and validates its primary key uniqueness. The
/// reference sequences and lineage tables of a table other than the default table are prefixed
/// with its name (see `initialize::Initializer::preprocessedName`), so that several tables can be
/// preprocessed into the same database. Throws on invalid input, which may leave `database`
/// partially filled.
void preprocessing(
   Database& database,
   const schema::TableName& table_name,
   const config::InitializationFiles& initialization_files,
   const std::optional<std::filesystem::path>& input_file
);

}  // namespace rhydb::preprocessing
