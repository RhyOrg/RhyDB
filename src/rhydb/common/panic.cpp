#include "rhydb/common/panic.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include <fmt/format.h>

namespace rhydb::common {

namespace {

[[noreturn]] void panic(
   const std::string& prefix,
   const std::string& msg,
   const char* file,
   int line
) {
   const char* env = getenv("RHYDB_PANIC");
   bool do_abort;
   if (env) {
      do_abort = strcmp(env, "abort") == 0;
   } else {
      do_abort = false;
   }
   auto full_msg = fmt::format("{}{} at {}:{}", prefix, msg, file, line);
   if (do_abort) {
      std::cerr << full_msg << "\n" << std::flush;
      abort();
   }
   throw std::runtime_error(full_msg);
}

}  // namespace

[[noreturn]] void panic(const std::string& msg, const char* file, int line) {
   panic("PANIC: ", msg, file, line);
}

[[noreturn]] void todo(const char* file, int line) {
   panic("TODO: ", "This execution path is not implemented yet", file, line);
}

[[noreturn]] void unreachable(const char* file, int line) {
   panic(
      "UNREACHABLE: ",
      "Please report this as a bug in RhyDB: this code should never be reachable",
      file,
      line
   );
}

[[noreturn]] void unimplemented(const char* file, int line) {
   panic("UNIMPLEMENTED: ", "This execution path is not implemented", file, line);
}

[[noreturn]] void assertFailure(const char* msg, const char* file, int line) {
   panic("ASSERT failure: ", msg, file, line);
}

[[noreturn]] void assertOpFailure(
   const char* prefix,
   const char* e1_str,
   const char* op_str,
   const char* e2_str,
   const std::string& values,
   const char* file,
   int line
) {
   panic(fmt::format("{} failure: {} {} {}: ", prefix, e1_str, op_str, e2_str), values, file, line);
}

[[noreturn]] void debugAssertFailure(const char* msg, const char* file, int line) {
   panic("DEBUG_ASSERT failure: ", msg, file, line);
}

}  // namespace rhydb::common
