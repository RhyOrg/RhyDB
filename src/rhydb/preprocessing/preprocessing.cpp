#include "rhydb/preprocessing/preprocessing.h"

#include "rhydb/append/append_exception.h"
#include "rhydb/append/ndjson_line_reader.h"
#include "rhydb/common/input_stream_wrapper.h"
#include "rhydb/initialize/initialize_exception.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/preprocessing/preprocessing_exception.h"
#include "rhydb/schema/duplicate_primary_key_exception.h"

namespace rhydb::preprocessing {

Database preprocessing(const config::PreprocessingConfig& preprocessing_config) {
   Database database;
   preprocessing(
      database, preprocessing_config.initialization_files, preprocessing_config.getInputFilePath()
   );
   return database;
}

void preprocessing(
   Database& database,
   const config::InitializationFiles& initialization_files,
   const std::optional<std::filesystem::path>& input_file
) {
   try {
      SPDLOG_INFO("preprocessing - initializing Database");
      initialize::Initializer::createTableInDatabase(
         schema::TableName::getDefault(), initialization_files, database
      );

      SPDLOG_INFO("preprocessing - successfully initialized Database, now opening input");
      auto input = InputStreamWrapper::openFileOrStdIn(input_file);

      SPDLOG_INFO("preprocessing - appending data to Database");
      database.appendData(schema::TableName::getDefault(), input.getInputStream());

      SPDLOG_INFO("preprocessing - validating primary key uniqueness");
      database.tables.at(schema::TableName::getDefault())->validatePrimaryKeyUnique();

      SPDLOG_INFO("preprocessing - finished preprocessing");
   } catch (const initialize::InitializeException& exception) {
      throw PreprocessingException(
         "preprocessing - exception when initializing database: {}", exception.what()
      );
   } catch (const append::AppendException& exception) {
      throw PreprocessingException(
         "preprocessing - exception when appending data: {}", exception.what()
      );
   } catch (const schema::DuplicatePrimaryKeyException& exception) {
      throw PreprocessingException(
         "preprocessing - primary key uniqueness validation failed: {}", exception.what()
      );
   }
}

}  // namespace rhydb::preprocessing
