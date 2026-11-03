#include "cors_policy.h"

#include <algorithm>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "config/config_exception.h"

namespace rhydb_app {

using rhydb::config::ConfigException;

namespace {

// Matches an exact origin: scheme://host[:port]. No trailing slash, no path, no query.
// Capture groups: 1 = name/IPv4 host (empty for IPv6), 2 = bracketed IPv6 literal without brackets,
// 3 = port digits (empty if none). The host is either a name/IPv4 (letters, digits, dots, hyphens,
// covering domain names and `localhost`) or a bracketed IPv6 literal such as `[::1]`. The IPv6
// contents and the port range are validated separately below.
const std::regex ORIGIN_PATTERN{
   R"(^[a-zA-Z][a-zA-Z0-9+.-]*://(?:([a-zA-Z0-9.-]+)|\[([0-9a-fA-F:]+)\])(?::([0-9]+))?$)"
};

// Matches an IPv6 literal (RFC 4291): eight hextets or a single `::` compression. Strict enough to
// reject obvious junk like `:`, `[:]`, or more than one `::`. A trailing dotted IPv4 suffix is not
// accepted; browsers normalise such origins to pure hex form anyway.
const std::regex IPV6_PATTERN{
   R"(^(?:)"
   R"((?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4})"               // 1:2:3:4:5:6:7:8
   R"(|(?:[0-9a-fA-F]{1,4}:){1,7}:)"                           // 1::            1:2:3:4:5:6:7::
   R"(|(?:[0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4})"           // 1::8           1:2:3:4:5:6::8
   R"(|(?:[0-9a-fA-F]{1,4}:){1,5}(?::[0-9a-fA-F]{1,4}){1,2})"  // 1::7:8         ...
   R"(|(?:[0-9a-fA-F]{1,4}:){1,4}(?::[0-9a-fA-F]{1,4}){1,3})"  //
   R"(|(?:[0-9a-fA-F]{1,4}:){1,3}(?::[0-9a-fA-F]{1,4}){1,4})"  //
   R"(|(?:[0-9a-fA-F]{1,4}:){1,2}(?::[0-9a-fA-F]{1,4}){1,5})"  //
   R"(|[0-9a-fA-F]{1,4}:(?::[0-9a-fA-F]{1,4}){1,6})"           // 1::3:4:5:6:7:8
   R"(|:(?:(?::[0-9a-fA-F]{1,4}){1,7}|:))"                     // ::8  ::  ::1:2:...
   R"()$)"
};

constexpr uint32_t MAX_PORT = 65535;

bool isValidOrigin(const std::string& origin) {
   std::smatch match;
   if (!std::regex_match(origin, match, ORIGIN_PATTERN)) {
      return false;
   }

   const std::string ipv6_host = match[2].str();
   if (!ipv6_host.empty() && !std::regex_match(ipv6_host, IPV6_PATTERN)) {
      return false;
   }

   const std::string port = match[3].str();
   if (!port.empty()) {
      // The regex guarantees the port is digits only; reject values out of the TCP port range.
      try {
         if (std::stoul(port) > MAX_PORT) {
            return false;
         }
      } catch (const std::out_of_range&) {
         return false;
      }
   }

   return true;
}

}  // namespace

CorsPolicy::CorsPolicy(Mode mode, std::vector<std::string> allowed_origins)
    : mode(mode),
      allowed_origins(std::move(allowed_origins)) {}

CorsPolicy CorsPolicy::parse(std::string_view config_value) {
   std::vector<std::string> entries;
   boost::split(entries, config_value, boost::is_any_of(","));
   for (auto& entry : entries) {
      boost::trim(entry);
   }

   const bool has_wildcard =
      std::any_of(entries.begin(), entries.end(), [](const auto& entry) { return entry == "*"; });

   if (has_wildcard) {
      if (entries.size() != 1) {
         throw ConfigException{
            "Invalid value for 'api.corsAllowedOrigins': '*' cannot be combined with explicit "
            "origins."
         };
      }
      return CorsPolicy{Mode::ANY, {}};
   }

   for (const auto& entry : entries) {
      if (entry.empty()) {
         throw ConfigException{
            "Invalid value for 'api.corsAllowedOrigins': it must not contain empty origins."
         };
      }
      if (!isValidOrigin(entry)) {
         throw ConfigException{
            "Invalid value for 'api.corsAllowedOrigins': '" + entry +
            "' is not a valid origin. Expected the exact form 'scheme://host[:port]' without a "
            "trailing slash or path."
         };
      }
   }

   return CorsPolicy{Mode::LIST, std::move(entries)};
}

bool CorsPolicy::allowsAnyOrigin() const {
   return mode == Mode::ANY;
}

bool CorsPolicy::isAllowed(const std::string& origin) const {
   if (mode == Mode::ANY) {
      return true;
   }
   return std::find(allowed_origins.begin(), allowed_origins.end(), origin) !=
          allowed_origins.end();
}

}  // namespace rhydb_app
