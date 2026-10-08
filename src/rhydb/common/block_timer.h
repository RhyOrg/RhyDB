#pragma once

#include <chrono>
#include <string>

namespace rhydb::common {

template <typename Unit = std::chrono::microseconds, typename Clock = std::chrono::steady_clock>
struct [[nodiscard]] BlockTimer {
   using time_point_t = Clock::time_point;
   using output_t = Clock::rep;

   output_t& output_reference;
   time_point_t start;

   explicit BlockTimer(output_t& output_reference)
       : output_reference(output_reference),
         start(Clock::now()) {}

   ~BlockTimer() {
      auto end = Clock::now();
      output_reference = std::chrono::duration_cast<Unit>(end - start).count();
   }

   output_t untilNow() { return std::chrono::duration_cast<Unit>(Clock::now() - start).count(); }
};

std::string formatDuration(int64_t int_microseconds);

}  // namespace rhydb::common
