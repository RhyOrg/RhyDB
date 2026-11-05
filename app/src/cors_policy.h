#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rhydb_app {

/// Decides which request origins are allowed for Cross-Origin Resource Sharing (CORS).
///
/// Built from the `api.corsAllowedOrigins` option, which is either `*` (allow any origin) or a
/// comma-separated list of exact origins (e.g. `https://example.org,https://other.org:8443`).
/// Configured origins are normalized at parse time to the canonical form a browser sends in the
/// `Origin` header (lowercase scheme and host, default port omitted), so matching against the
/// incoming origin is then an exact, case-sensitive string comparison.
///
/// CORS is an HTTP-API concern, so this lives in the API layer rather than the portable core. The
/// core only carries the raw `api.corsAllowedOrigins` string; it is parsed into a policy here.
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

}  // namespace rhydb_app
