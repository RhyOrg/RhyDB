# Core RhyDB library (rhydblib). Consumer by several targets.
# Should be agnostic to those targets.

# ---------------------------------------------------------------------------
# rhydblib dependencies
# ---------------------------------------------------------------------------

find_package(Arrow REQUIRED COMPONENTS Acero)
find_package(Boost REQUIRED COMPONENTS system serialization iostreams)
find_package(FastFloat REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(re2 REQUIRED)
find_package(roaring REQUIRED)
find_package(spdlog REQUIRED)
find_package(simdjson REQUIRED)
find_package(simdutf REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(zstd REQUIRED)

# Native-only: lzma-compressed inputs. Not available for Emscripten (the browser
# build accepts raw and .zst inputs only).
if(NOT EMSCRIPTEN)
    find_package(LibLZMA REQUIRED)
endif()

# ---------------------------------------------------------------------------
# rhydblib
# ---------------------------------------------------------------------------

add_library(rhydblib_deps INTERFACE)

target_link_libraries(rhydblib_deps INTERFACE
        Arrow::arrow_static
        ArrowAcero::arrow_acero_static
        ${Boost_LIBRARIES}
        ${FastFloat_LIBRARIES}
        nlohmann_json::nlohmann_json
        ${roaring_LIBRARIES}
        ${spdlog_LIBRARIES}
        ${simdjson_LIBRARIES}
        ${simdutf_LIBRARIES}
        ${yaml-cpp_LIBRARIES}
        zstd::libzstd_static
        re2::re2
)

target_include_directories(rhydblib_deps INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(rhydblib_deps INTERFACE
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
        SIMDJSON_EXCEPTIONS=0
)

# Extra flags that some targets might need.
# These are INTERFACE so they compile into the rhydblib objects too.
target_compile_options(rhydblib_deps INTERFACE ${RHYDB_CORE_EXTRA_COMPILE_OPTIONS})
target_compile_definitions(rhydblib_deps INTERFACE ${RHYDB_CORE_EXTRA_COMPILE_DEFINITIONS})

file(GLOB_RECURSE RHYDBLIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE RHYDB_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp")
list(REMOVE_ITEM RHYDBLIB_SOURCES ${RHYDB_TEST_SOURCES})

add_library(rhydblib OBJECT ${RHYDBLIB_SOURCES})
set_target_properties(rhydblib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rhydblib PUBLIC rhydblib_deps)

# ---------------------------------------------------------------------------
# Linter
# ---------------------------------------------------------------------------

# Apply clang-tidy (resolved by the root CMakeLists.txt) here, after rhydblib so
# its objects are not linted via the CXX_CLANG_TIDY property, but before the
# rhydb_test target below so the tests are. This matches the ordering from before
# the src/ split. The root re-applies it at its own scope for the consumers.
if (CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif ()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# Building the unit tests is opt-in and on by default.
option(BUILD_UNIT_TESTS "Build the RhyDB unit tests" ON)
if(BUILD_UNIT_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include_directories(${GTest_INCLUDE_DIRS})

    add_executable(rhydb_test ${RHYDB_TEST_SOURCES})
    if (NOT GTest_LIBRARIES)
        set(GTest_LIBRARIES gtest gmock)
    endif ()
    target_link_libraries(rhydb_test PUBLIC ${GTest_LIBRARIES} rhydblib)
endif()
