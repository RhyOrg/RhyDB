find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

# Find Cython
execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import Cython; print(Cython.__version__)"
        OUTPUT_VARIABLE CYTHON_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CYTHON_NOT_FOUND
)

if(CYTHON_NOT_FOUND)
    message(FATAL_ERROR "Cython not found. Install with: pip install Cython")
endif()

message(STATUS "Found Cython version: ${CYTHON_VERSION}")

# Cython modules to build
set(CYTHON_MODULES database)

# Build each Cython module
foreach(module ${CYTHON_MODULES})
    set(pyx_file "${CMAKE_SOURCE_DIR}/python/rhydb/${module}.pyx")
    set(cpp_file "${CMAKE_BINARY_DIR}/python/rhydb/${module}.cpp")

    # Run Cython to generate C++ file
    add_custom_command(
            OUTPUT ${cpp_file}
            COMMAND ${Python3_EXECUTABLE} -m cython
            --cplus
            -3
            -I "${CMAKE_SOURCE_DIR}/python/rhydb"
            -o ${cpp_file}
            ${pyx_file}
            DEPENDS ${pyx_file}
            COMMENT "Cythonizing ${module}.pyx"
    )

    # Create Python extension module
    Python3_add_library(${module} MODULE ${cpp_file})

    # Set include directories
    target_include_directories(${module} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/python/rhydb
    )

    # Link against the portable core directly: the bindings only touch
    # rhydb::Database (preprocessing, append, query, save/load), all of which
    # live in rhydblib. They need none of the Poco HTTP API layer, so linking
    # rhydblib keeps Poco out of the Python extension. Linking the OBJECT
    # library directly pulls in its objects and usage requirements.
    target_link_libraries(${module} PRIVATE rhydblib)

    # Export only PyInit_<module>, the extension's sole entry point (as pybind11 and nanobind do by
    # default). Otherwise the symbols of statically linked dependencies leak out, and on macOS the
    # loader merges their weak definitions with those of previously loaded libraries. E.g. after
    # `import pyarrow`, RE2 would call pyarrow's incompatible Abseil and crash.
    if(APPLE)
        target_link_options(${module} PRIVATE "LINKER:-exported_symbol,_PyInit_${module}")
    elseif(UNIX)
        set(version_script "${CMAKE_BINARY_DIR}/python/${module}.version")
        file(WRITE ${version_script} "{ global: PyInit_${module}; local: *; };\n")
        target_link_options(${module} PRIVATE "LINKER:--version-script=${version_script}")
    endif()

    # Set output name and location
    set_target_properties(${module} PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${module}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/rhydb"
    )

    # Install to Python package directory
    # Note: CMAKE_INSTALL_PREFIX is set by setup.py to point to the rhydb directory
    install(TARGETS ${module}
            LIBRARY DESTINATION .
    )
endforeach()

# Install Python package files
install(FILES "${CMAKE_SOURCE_DIR}/python/rhydb/__init__.py"
        DESTINATION .
)
