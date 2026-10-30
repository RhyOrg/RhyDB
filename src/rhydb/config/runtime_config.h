#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "config/config_specification.h"

namespace rhydb::config {

class ApiOptions {
  public:
   int32_t max_connections;
   int32_t parallel_threads;
   std::string address;
   uint16_t port;
   std::optional<std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>>
      estimated_startup_end;
   uint32_t soft_memory_limit;
   /// Whether the write-enabled `POST /admin/query` endpoint is served. Opt-in, so an instance
   /// that does not enable it stays strictly read-only.
   bool allow_admin_endpoint;
   /// Raw value of the `api.corsAllowedOrigins` option. Unset means CORS is disabled, so no
   /// CORS headers are sent. When set it is either `*` or a comma-separated list of origins; see
   /// rhydb::config::CorsPolicy.
   std::optional<std::string> cors_allowed_origins;
};

class QueryOptions {
  public:
   size_t materialization_cutoff;
};

class RuntimeConfig {
   RuntimeConfig() = default;

  public:
   std::filesystem::path data_directory;
   ApiOptions api_options;
   QueryOptions query_options;

   static RuntimeConfig withDefaults();

   static ConfigSpecification getConfigSpecification();

   void validate() const;

   [[nodiscard]] static std::vector<std::filesystem::path> getConfigFilePaths(
      const VerifiedCommandLineArguments& cmd_source,
      const VerifiedConfigAttributes& env_source
   );

   void overwriteFrom(const VerifiedConfigAttributes& config_source);
};

}  // namespace rhydb::config

template <>
struct [[maybe_unused]] fmt::formatter<rhydb::config::RuntimeConfig> : fmt::formatter<std::string> {
   [[maybe_unused]] static auto format(
      const rhydb::config::RuntimeConfig& runtime_config,
      format_context& ctx
   ) -> decltype(ctx.out());
};
