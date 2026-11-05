#include "cors_policy.h"

#include <algorithm>
#include <cstdint>
#include <regex>
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

bool hasExplicitZeroPort(const std::string& origin) {
   static const std::regex zero_port{":0+$"};
   return std::regex_search(origin, zero_port);
}

// Serializes a parsed origin to its canonical form, matching how a browser serializes the `Origin`
// header: lowercase scheme and host, an IPv6 host wrapped in brackets, and the port omitted when it
// is the scheme's default. Poco::URI already lowercases the scheme and host while parsing.
std::string serializeOrigin(const Poco::URI& uri) {
   const std::string& host = uri.getHost();
   const std::string bracketed_host = isIpv6Host(host) ? "[" + host + "]" : host;

   std::string serialized = uri.getScheme() + "://" + bracketed_host;

   const uint16_t specified_port = uri.getSpecifiedPort();
   if (specified_port != 0) {
      const uint16_t default_port = Poco::URI(uri.getScheme() + "://" + bracketed_host).getPort();
      if (specified_port != default_port) {
         serialized += ":" + std::to_string(specified_port);
      }
   }
   return serialized;
}

// An exact origin is `scheme://host[:port]` with nothing else: no path, query, fragment or
// userinfo. Poco::URI parses the structure and rejects out-of-range ports; it does not validate the
// contents of an IPv6 host, so that is checked separately. On success the canonical serialization
// is written to `normalized` so configured origins match the `Origin` header browsers send.
bool normalizeOrigin(const std::string& origin, std::string& normalized) {
   try {
      const Poco::URI uri{origin};

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
      if (isIpv6Host(host) && !isValidIpv6(host)) {
         return false;
      }

      // Poco cannot distinguish an explicit `:0` from an absent port, so reject it explicitly.
      if (hasExplicitZeroPort(origin)) {
         return false;
      }

      normalized = serializeOrigin(uri);
      return true;
   } catch (const Poco::Exception&) {
      return false;
   }
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

   std::vector<std::string> normalized_origins;
   normalized_origins.reserve(entries.size());
   for (const auto& entry : entries) {
      if (entry.empty()) {
         throw ConfigException{
            "Invalid value for 'api.corsAllowedOrigins': it must not contain empty origins."
         };
      }
      std::string normalized;
      if (!normalizeOrigin(entry, normalized)) {
         throw ConfigException{
            "Invalid value for 'api.corsAllowedOrigins': '" + entry +
            "' is not a valid origin. Expected the exact form 'scheme://host[:port]' without a "
            "trailing slash or path."
         };
      }
      normalized_origins.push_back(std::move(normalized));
   }

   return CorsPolicy{Mode::LIST, std::move(normalized_origins)};
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
