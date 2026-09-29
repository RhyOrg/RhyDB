#ifdef __linux__
#include <malloc.h>
#endif

#include <spdlog/spdlog.h>

namespace rhydb::common {

class Allocator {
  public:
#ifdef __linux__
   static void trim() {
      SPDLOG_INFO("Manually invoking malloc_trim() to give back memory to OS.");
      malloc_trim(0);
   }
#else
   static void trim() { SPDLOG_INFO("Allocator::trim() is not implemented for this platform."); }
#endif
};

}  // namespace rhydb::common