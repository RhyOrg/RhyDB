#include <filesystem>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <arrow/compute/api.h>

#include <config/config_interface.h>
#include <rhydb/append/append.h>
#include <rhydb/common/overloaded.h>
#include <rhydb/common/panic.h>
#include <rhydb/common/version.h>
#include <rhydb/config/append_config.h>
#include <rhydb/config/initialize_config.h>
#include <rhydb/config/preprocessing_config.h>
#include <rhydb/config/runtime_config.h>
#include <rhydb/database.h>
#include <rhydb/initialize/initialize_exception.h>
#include <rhydb/initialize/initializer.h>
#include <rhydb/preprocessing/preprocessing.h>
#include <rhydb/preprocessing/preprocessing_exception.h>
#include <evobench/evobench.hpp>

#include "api.h"
#include "logging.h"

namespace {

/// Does not throw exceptions
int runInitializer(const rhydb::config::InitializeConfig& initialize_config) {
   EVOBENCH_SCOPE("top-level", "runInitializer");
   try {
      auto database = std::make_shared<rhydb::Database>();
      // TODO(#1091) make this configurable
      const auto& table_name = rhydb::schema::TableName::getDefault();
      rhydb::initialize::Initializer::createTableInDatabase(
         table_name, initialize_config.initialization_files, *database
      );
      database->saveDatabaseState(initialize_config.output_directory);
      return 0;
   } catch (const rhydb::initialize::InitializeException& preprocessing_exception) {
      SPDLOG_ERROR("initialize - error: {}", preprocessing_exception.what());
      return 1;
   }
}

int runPreprocessor(const rhydb::config::PreprocessingConfig& preprocessing_config) {
   EVOBENCH_SCOPE("top-level", "runPreprocessor");
   try {
      auto database = rhydb::preprocessing::preprocessing(preprocessing_config);
      database.saveDatabaseState(preprocessing_config.output_directory);
      return 0;
   } catch (const rhydb::preprocessing::PreprocessingException& preprocessing_exception) {
      SPDLOG_ERROR("preprocessing - error: {}", preprocessing_exception.what());
      return 1;
   }
}

int runAppend(const rhydb::config::AppendConfig& append_config) {
   EVOBENCH_SCOPE("top-level", "runAppend");
   return rhydb::append::runAppend(append_config);
}

int runApi(const rhydb::config::RuntimeConfig& runtime_config) {
   rhydb_app::Api server;
   return server.runApi(runtime_config);
}

enum class ExecutionMode : uint8_t { INITIALIZE, APPEND, API, PREPROCESSING };

int mainWhichMayThrowExceptions(int argc, char** argv) {
   std::vector<std::string> all_args(argv, argv + argc);

   const std::filesystem::path program_path{all_args[0]};

   const std::string program_name = program_path.filename();

   std::span<const std::string> args(all_args.begin() + 1, all_args.end());

   if (!args.empty() && (args[0] == "--version" || args[0] == "-V")) {
      fmt::print("{} {}\n", program_name, rhydb::RELEASE_VERSION);
      return 0;
   }

   setupLogger();
   RHYDB_ASSERT(arrow::compute::Initialize().ok());

   ExecutionMode mode;
   if (args.empty()) {
      std::cerr << program_name
                << ": need 'preprocessing', 'initialize', 'append' or 'api' as the first program "
                   "argument.\n";
      return 1;
   }

   const std::string& mode_argument = args[0];
   args = {args.begin() + 1, args.end()};
   if (mode_argument == "preprocessing") {
      mode = ExecutionMode::PREPROCESSING;
   } else if (mode_argument == "append") {
      mode = ExecutionMode::APPEND;
   } else if (mode_argument == "api") {
      mode = ExecutionMode::API;
   } else if (mode_argument == "initialize") {
      mode = ExecutionMode::INITIALIZE;
   } else {
      std::cerr << program_name
                << ": need 'preprocessing', 'initialize', 'append' or 'api' as the first program "
                   "argument, got '"
                << mode_argument << "'\n";
      return 1;
   }

   SPDLOG_INFO("Starting RhyDB (version {})", rhydb::RELEASE_VERSION);

   std::vector<std::string> env_allow_list;
   env_allow_list.emplace_back("RHYDB_PANIC");
   for (auto& field :
        rhydb::config::PreprocessingConfig::getConfigSpecification().attribute_specifications) {
      env_allow_list.emplace_back(
         rhydb::config::EnvironmentVariables::configKeyPathToString(field.key)
      );
   }
   for (auto& field :
        rhydb::config::RuntimeConfig::getConfigSpecification().attribute_specifications) {
      env_allow_list.emplace_back(
         rhydb::config::EnvironmentVariables::configKeyPathToString(field.key)
      );
   }

   switch (mode) {
      case ExecutionMode::PREPROCESSING:
         return std::visit(
            Overloaded{
               [&](const rhydb::config::PreprocessingConfig& preprocessing_config) {
                  SPDLOG_INFO("preprocessing_config = {}", preprocessing_config);
                  return runPreprocessor(preprocessing_config);
               },
               [&](int32_t exit_code) { return exit_code; },
            },
            rhydb::config::getConfig<rhydb::config::PreprocessingConfig>(args, env_allow_list)
         );
      case ExecutionMode::INITIALIZE:
         return std::visit(
            Overloaded{
               [&](const rhydb::config::InitializeConfig& initialize_config) {
                  SPDLOG_INFO("initialize_config = {}", initialize_config);
                  return runInitializer(initialize_config);
               },
               [&](int32_t exit_code) { return exit_code; },
            },
            rhydb::config::getConfig<rhydb::config::InitializeConfig>(args, env_allow_list)
         );
      case ExecutionMode::APPEND:
         return std::visit(
            Overloaded{
               [&](const rhydb::config::AppendConfig& append_config) {
                  SPDLOG_INFO("append_config = {}", append_config);
                  return runAppend(append_config);
               },
               [&](int32_t exit_code) { return exit_code; },
            },
            rhydb::config::getConfig<rhydb::config::AppendConfig>(args, env_allow_list)
         );
      case ExecutionMode::API:
         return std::visit(
            Overloaded{
               [&](const rhydb::config::RuntimeConfig& runtime_config) {
                  SPDLOG_INFO("runtime_config = {}", runtime_config);
                  return runApi(runtime_config);
               },
               [&](int32_t exit_code) { return exit_code; },
            },
            rhydb::config::getConfig<rhydb::config::RuntimeConfig>(args, env_allow_list)
         );
   }
   RHYDB_UNREACHABLE();
}

}  // namespace

int main(int argc, char** argv) {
   try {
      return mainWhichMayThrowExceptions(argc, argv);
   } catch (const std::exception& error) {
      SPDLOG_ERROR("Internal Error: {}", error.what());
      return 2;
   }
}
