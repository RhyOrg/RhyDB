#pragma once

#include <filesystem>
#include <optional>

#include "rhydb/config/preprocessing_config.h"

#include "rhydb/database.h"

namespace rhydb::preprocessing {

Database preprocessing(const config::PreprocessingConfig& preprocessing_config);

/// Creates the default table in `database` from `initialization_files`, appends the NDJSON data of
/// `input_file` (stdin if not given) to it and validates its primary key uniqueness. The database's
/// built-in tables must still be empty. Throws a PreprocessingException on invalid input, which
/// may leave `database` partially filled.
void preprocessing(
   Database& database,
   const config::InitializationFiles& initialization_files,
   const std::optional<std::filesystem::path>& input_file
);

}  // namespace rhydb::preprocessing
