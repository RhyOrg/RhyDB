#include "rhydb/config/cors_policy.h"

#include <algorithm>
#include <regex>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "config/config_exception.h"

namespace rhydb::config {

namespace {

// Matches an exact origin: scheme://host[:port]. No trailing slash, no path, no query.
// Host allows letters, digits, dots and hyphens, which covers domain names, IPv4 and `localhost`.
const std::regex ORIGIN_PATTERN{R"(^[a-zA-Z][a-zA-Z0-9+.-]*://[a-zA-Z0-9.-]+(:[0-9]+)?$)"};

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
      if (!std::regex_match(entry, ORIGIN_PATTERN)) {
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

}  // namespace rhydb::config
