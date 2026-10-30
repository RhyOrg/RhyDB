#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rhydb::config {

/// Decides which request origins are allowed for Cross-Origin Resource Sharing (CORS).
///
/// Built from the `api.corsAllowedOrigins` option, which is either `*` (allow any origin) or a
/// comma-separated list of exact origins (e.g. `https://example.org,https://other.org:8443`).
/// Origin matching is exact and case-sensitive; browsers send the origin without a trailing slash,
/// so configured origins must not have one either.
class CorsPolicy {
   enum class Mode : uint8_t { ANY, LIST };

   Mode mode;
   std::vector<std::string> allowed_origins;

   CorsPolicy(Mode mode, std::vector<std::string> allowed_origins);

  public:
   /// Parses the raw config value into a policy.
   ///
   /// Throws rhydb::config::ConfigException if the value is malformed: empty entries, `*` mixed
   /// with explicit origins, or an entry that is not exactly `scheme://host[:port]`.
   static CorsPolicy parse(std::string_view config_value);

   /// Whether `Access-Control-Allow-Origin: *` is used instead of echoing the request origin.
   [[nodiscard]] bool allowsAnyOrigin() const;

   /// Whether the given request origin is allowed.
   [[nodiscard]] bool isAllowed(const std::string& origin) const;
};

}  // namespace rhydb::config
