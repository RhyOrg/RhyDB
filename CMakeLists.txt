cmake_minimum_required(VERSION 3.22)
project(RHYDB)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g -fsanitize=address -D RHYDB_DEBUG_ASSERTIONS=1")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -D RHYDB_DEBUG_ASSERTIONS=0")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For find_package calls in Module mode (looking for the Find<lib>.cmake files)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_BINARY_DIR}/generators/")
# For find_package calls in config mode / find_dependency calls
# (looking for the <lib>-config.cmake files of transitive dependencies of modules, e.g. spdlog->fmt)
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "${CMAKE_BINARY_DIR}/generators/")

# Export compile commands for configuring language servers or debugging the build
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Toolchain policy
# ---------------------------------------------------------------------------
# Top-level build setup that must be decided before the core library (src/) and
# the consumers are added. It conditions how the Emscripten/WebAssembly build is
# configured and exports the extra compile options/definitions that core applies
# to rhydblib (see src/CMakeLists.txt).

# Under the Emscripten toolchain, find_package defaults to searching only inside
# the cross-compilation sysroot (CMAKE_FIND_ROOT_PATH), so the Conan-generated
# package config files in build/wasm/generators are ignored. Add that directory
# as a find root and allow package lookups to fall back to CMAKE_PREFIX_PATH.
# This must run before any find_package call (i.e. before add_subdirectory(src)).
if(EMSCRIPTEN)
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_BINARY_DIR}/generators")
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
endif()

# WebAssembly-only usage requirements, exported for src/CMakeLists.txt to attach
# to rhydblib_deps (INTERFACE, so they compile into the rhydblib objects too):
# NO_EVOBENCH disables the evobench probes (the browser build does not profile),
# and -fexceptions must reach every rhydblib object (they throw). -pthread keeps
# RhyDB objects ABI-consistent with the Emscripten dependency builds. The build
# variant is detected via the compiler-defined __EMSCRIPTEN__ macro.
set(RHYDB_CORE_EXTRA_COMPILE_OPTIONS "")
set(RHYDB_CORE_EXTRA_COMPILE_DEFINITIONS "")
if(EMSCRIPTEN)
    list(APPEND RHYDB_CORE_EXTRA_COMPILE_DEFINITIONS NO_EVOBENCH)
    list(APPEND RHYDB_CORE_EXTRA_COMPILE_OPTIONS -fexceptions -pthread)
endif()

# ---------------------------------------------------------------------------
# Version
# ---------------------------------------------------------------------------

if (EXISTS "${CMAKE_SOURCE_DIR}/release_version.txt")
    file(READ "${CMAKE_SOURCE_DIR}/release_version.txt" VERSION)
    string(STRIP "${VERSION}" VERSION)
    message(STATUS "Detected version from release_version.txt: ${VERSION}")
    add_compile_definitions(RHYDB_RELEASE_VERSION="${VERSION}")
else ()
    message(STATUS "No release_version.txt available for setting the version")
endif()

# ---------------------------------------------------------------------------
# Linter
# ---------------------------------------------------------------------------

# Resolve the clang-tidy executable here, but do NOT set CMAKE_CXX_CLANG_TIDY
# yet: CMAKE_CXX_CLANG_TIDY only affects targets created after it is set, and
# before the src/ split the property was enabled after rhydblib (so rhydblib's
# own objects were never linted via it) but before rhydb_test and the consumer
# targets (which were). To preserve that exactly across directory scopes we pass
# the resolved path down to src/ (applied after rhydblib, picked up by
# rhydb_test) and re-apply it at this scope after add_subdirectory(src) so the
# consumers below inherit it too.
option(BUILD_WITH_CLANG_TIDY "Build process clang-tidy")
if (NOT CMAKE_BUILD_TYPE STREQUAL Release AND BUILD_WITH_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy-19 clang-tidy-20)
    if (NOT CLANG_TIDY_EXE)
        message(SEND_ERROR "clang-tidy not found, aborting. You can run the build with '-D BUILD_WITH_CLANG_TIDY=OFF' to disable clang-tidy.")
    else ()
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        message(STATUS "run clang tidy with: ${CLANG_TIDY_EXE}")
    endif ()
endif ()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------

# rhydblib and its dependencies, plus the rhydb_test unit tests. src/ applies the
# resolved CLANG_TIDY_EXE after rhydblib so rhydb_test (but not rhydblib) is
# linted, matching the pre-split ordering. The consumers below link against
# rhydblib.
add_subdirectory(src)

# Re-apply clang-tidy at this scope so the consumer targets added below inherit
# it (subdirectory scopes do not propagate variables back to the parent).
if (CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif ()

# ---------------------------------------------------------------------------
# Consumers
# ---------------------------------------------------------------------------

if(EMSCRIPTEN)
    # The browser embind module, a consumer of the shared rhydblib declaration.
    add_subdirectory(wasm)
else()
    # The native command-line application (the `rhydb` executable) is a consumer
    # of the shared rhydblib declaration
    add_subdirectory(app)

    # The performance benchmark targets
    add_subdirectory(performance)

    # The python binding target
    option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
    if(BUILD_PYTHON_BINDINGS)
        add_subdirectory(python)
    endif()
endif()
