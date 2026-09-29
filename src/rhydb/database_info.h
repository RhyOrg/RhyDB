#pragma once

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace rhydb {

struct DatabaseInfo {
   std::string_view version;
   uint32_t row_count;
   uint64_t vertical_bitmaps_size;
   uint64_t horizontal_bitmaps_size;
};

// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
void to_json(nlohmann::json& json, const rhydb::DatabaseInfo& databaseInfo);

}  // namespace rhydb

template <>
class [[maybe_unused]] fmt::formatter<rhydb::DatabaseInfo> {
  public:
   static constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
   [[maybe_unused]] static auto format(rhydb::DatabaseInfo database_info, format_context& ctx)
      -> decltype(ctx.out()) {
      return fmt::format_to(ctx.out(), "{}", nlohmann::json{database_info}.dump());
   }
};
