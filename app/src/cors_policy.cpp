#include "cors_policy.h"

#include <algorithm>
#include <string>

#include <Poco/Exception.h>
#include <Poco/Net/IPAddress.h>
#include <Poco/URI.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "config/config_exception.h"

namespace rhydb_app {

using rhydb::config::ConfigException;

namespace {

bool isIpv6Host(const std::string& host) {
   // Poco::URI stores an IPv6 host without its brackets, so it is the only host form that can
   // contain a colon.
   return host.contains(':');
}

bool isValidIpv6(const std::string& host) {
   Poco::Net::IPAddress parsed;
   return Poco::Net::IPAddress::tryParse(host, parsed) &&
          parsed.family() == Poco::Net::IPAddress::IPv6;
}

// An exact origin is `scheme://host[:port]` with nothing else: no path, query, fragment or
// userinfo. Poco::URI parses the structure and rejects out-of-range ports; it does not validate the
// contents of an IPv6 host, so that is checked separately.
bool isValidOrigin(const std::string& origin) {
   Poco::URI uri;
   try {
      uri = Poco::URI(origin);
   } catch (const Poco::Exception&) {
      return false;
   }

   if (uri.getScheme().empty() || uri.getHost().empty()) {
      return false;
   }
   if (
      !uri.getPath().empty() || !uri.getRawQuery().empty() || !uri.getFragment().empty() ||
      !uri.getUserInfo().empty()
   ) {
      return false;
   }

   const std::string& host = uri.getHost();
   return !isIpv6Host(host) || isValidIpv6(host);
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
      std::ranges::any_of(entries, [](const auto& entry) { return entry == "*"; });

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
   return std::ranges::find(allowed_origins, origin) != allowed_origins.end();
}

}  // namespace rhydb_app
