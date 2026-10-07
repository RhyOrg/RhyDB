#include "rhydb/common/date32.h"

#include <charconv>
#include <chrono>
#include <expected>
#include <string>

#include <fmt/format.h>

namespace rhydb::common {

std::expected<Date32, std::string> stringToDate32(std::string_view value) {
   if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
      return std::unexpected{
         fmt::format("Invalid date format '{}': expected exactly YYYY-MM-DD", value),
      };
   }
   const std::string_view year_sv = value.substr(0, 4);
   const std::string_view month_sv = value.substr(5, 2);
   const std::string_view day_sv = value.substr(8, 2);

   int year;
   auto [ptr_y, ec_y] = std::from_chars(year_sv.data(), year_sv.data() + year_sv.size(), year);
   if (ec_y != std::errc{} || ptr_y != year_sv.data() + year_sv.size()) {
      return std::unexpected{fmt::format("Failed to parse year in date '{}'", value)};
   }

   int month;
   auto [ptr_m, ec_m] = std::from_chars(month_sv.data(), month_sv.data() + month_sv.size(), month);
   if (ec_m != std::errc{} || ptr_m != month_sv.data() + month_sv.size()) {
      return std::unexpected{fmt::format("Failed to parse month in date '{}'", value)};
   }

   int day;
   auto [ptr_d, ec_d] = std::from_chars(day_sv.data(), day_sv.data() + day_sv.size(), day);
   if (ec_d != std::errc{} || ptr_d != day_sv.data() + day_sv.size()) {
      return std::unexpected{fmt::format("Failed to parse day in date '{}'", value)};
   }

   const std::chrono::year_month_day ymd{
      std::chrono::year{year},
      std::chrono::month{static_cast<unsigned>(month)},
      std::chrono::day{static_cast<unsigned>(day)},
   };
   if (!ymd.ok()) {
      return std::unexpected{fmt::format("Invalid calendar date '{}'", value)};
   }

   const auto days_since_epoch = std::chrono::sys_days{ymd}.time_since_epoch();
   return static_cast<Date32>(days_since_epoch.count());
}

std::string date32ToString(Date32 date) {
   const std::chrono::year_month_day ymd{std::chrono::sys_days{std::chrono::days{date}}};
   return fmt::format(
      "{:04}-{:02}-{:02}",
      static_cast<int>(ymd.year()),
      static_cast<unsigned>(ymd.month()),
      static_cast<unsigned>(ymd.day())
   );
}

}  // namespace rhydb::common
