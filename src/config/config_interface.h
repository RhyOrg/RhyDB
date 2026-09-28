#pragma once

#include <iostream>
#include <optional>
#include <span>
#include <variant>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "config/config_exception.h"
#include "config/config_specification.h"
#include "config/source/command_line_arguments.h"
#include "config/source/environment_variables.h"
#include "config/source/yaml_file.h"

namespace rhydb::config {

/// For config structs (possibly containing config file paths).
// We use a concept instead of virtual methods, because the context of
// its usage (getConfig) is using a template anyways, due to the
// different return types (RuntimeConfig vs.
// PreprocessingConfig). Alternatively, making the constructor private
// and instead creating a factory method would also be possible.
template <typename C>
concept Config = requires(
   C config,
   const VerifiedConfigAttributes& config_source,
   const VerifiedCommandLineArguments& cmd_source
) {
   { C::withDefaults() } -> std::same_as<C>;

   { C::getConfigSpecification() } -> std::same_as<ConfigSpecification>;

   /// Vector of config files that the user gave (or that is provided
   /// by the type via its defaults) that should be loaded and shadowed
   /// in the order of the vector.
   {
      C::getConfigFilePaths(cmd_source, config_source)
   } -> std::same_as<std::vector<std::filesystem::path>>;

   /// Overwrite the fields of an instance of the target type; done
   /// that way so that multiple kinds of config sources can shadow
   /// each other's values by application in sequence. Does not throw
   /// exceptions, except overwriteFrom can call RHYDB_PANIC when there
   /// is an inconsistency (bug) between ConfigSpecification and
   /// overwriteFrom implementation.
   { config.overwriteFrom(config_source) } -> std::same_as<void>;

   /// Validation / Sanity checks about the values of this config
   { config.validate() } -> std::same_as<void>;
};

std::optional<std::filesystem::path> getConfigFilePath(
   const rhydb::config::ConfigKeyPath& config_key_path,
   const VerifiedCommandLineArguments& cmd_source,
   const VerifiedConfigAttributes& env_source
);

/// This function needs a reference to the (remaining) command line
/// arguments to be parsed, thus the application gets a chance to take
/// off some arguments first (like "api" or "preprocessing" in the
/// current RhyDB version).
///
/// In case of error, returns the exit code that the caller should
/// pass to exit(): 0 if the user gave --help, 1 in case of erroneous
/// usage (the error is already printed in that case).
template <Config C>
std::variant<C, int32_t> getConfig(
   std::span<const std::string> cmd,
   const std::vector<std::string>& allow_list_for_env_vars
) {
   const auto config_specification = C::getConfigSpecification();
   try {
      VerifiedCommandLineArguments cmd_source =
         CommandLineArguments{cmd}.verify(config_specification);
      if (cmd_source.verbose_count == 1) {
         spdlog::set_level(spdlog::level::debug);
      } else if (cmd_source.verbose_count >= 2) {
         spdlog::set_level(spdlog::level::trace);
      }
      if (cmd_source.asks_for_help) {
         std::cout << config_specification.helpText() << "\n" << std::flush;
         return 0;
      }
      if (!cmd_source.positional_arguments.empty()) {
         throw rhydb::config::ConfigException{
            fmt::format(
               "RhyDB does not expect positional arguments, found {}",
               nlohmann::json{cmd_source.positional_arguments}.dump()
            ),
         };
      }

      auto env_source =
         EnvironmentVariables::newWithAllowListAndEnv(allow_list_for_env_vars, environ)
            .verify(config_specification);

      auto config_paths = C::getConfigFilePaths(cmd_source, env_source);

      SPDLOG_TRACE("Now overwriting config from defaults");
      C config = C::withDefaults();
      for (auto config_path : config_paths) {
         SPDLOG_TRACE("Now overwriting config from yaml file '{}'", config_path);
         auto file_source = YamlFile::readFile(config_path).verify(config_specification);
         config.overwriteFrom(file_source);
      }
      SPDLOG_TRACE("Now overwriting config from environment variables");
      config.overwriteFrom(env_source);
      SPDLOG_TRACE("Now overwriting config from command line arguments");
      config.overwriteFrom(cmd_source);

      config.validate();

      return std::move(config);
   } catch (const rhydb::config::ConfigException& e) {
      std::cerr << fmt::format(
                      "Usage error: {}.\n\nRun with the --help option for help.\n", e.what()
                   )
                << std::flush;
      return 1;
   }
}

}  // namespace rhydb::config
