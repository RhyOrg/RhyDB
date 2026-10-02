# The WebAssembly (Emscripten) embind module. Like app/ and python/, this is a
# thin consumer of the portable rhydblib declared in the root CMakeLists.txt.
#
# This subdirectory is only added when configuring with the Emscripten toolchain
# (see the EMSCRIPTEN guard in the root CMakeLists.txt). The shared C++ standard,
# core dependencies, source globbing, and the NO_EVOBENCH define and
# -fexceptions/-pthread compile flags all come from rhydblib / rhydblib_deps.

add_executable(rhydb_wasm src/rhydb_wasm.cpp)
# Emit rhydb_wasm.js/.wasm at build/wasm/ (not build/wasm/wasm/) so the Makefile
# `wasm` target, dist copy, and node test find them at the historical path.
set_target_properties(rhydb_wasm PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Linking the OBJECT library directly pulls in $<TARGET_OBJECTS:rhydblib> plus its
# usage requirements: the "src" include root, the core link libraries, and the
# Emscripten-only defines and compile flags carried by rhydblib_deps. rhydb_wasm is
# the only link step, so -fexceptions/-pthread are repeated here as link options.
target_link_libraries(rhydb_wasm PRIVATE rhydblib)

# 64-bit WebAssembly (wasm64). RHYDB_WASM_MEMORY64 is declared in the root
# CMakeLists.txt and, when ON, compiles rhydblib with --target=wasm64. The final
# link must use the same ABI, so mirror the flag here. -sALLOW_MEMORY_GROWTH=1
# stays enabled in both variants; wasm64 raises the addressable ceiling above
# the 32-bit ~4 GB limit.
#
# With -sALLOW_MEMORY_GROWTH, Emscripten caps growth at MAXIMUM_MEMORY, which
# defaults to 2 GiB. For wasm64 that default would negate the whole point of the
# 64-bit build, so raise the ceiling past 4 GiB. The 32-bit build cannot address
# more than 4 GiB, so it keeps the default cap.
if(RHYDB_WASM_MEMORY64)
    set(RHYDB_WASM_MEMORY64_LINK_OPTIONS
        --target=wasm64
        # 16 GiB. wasm64 memory is still reserved lazily and only committed as it
        # grows, so a high ceiling does not inflate the baseline footprint.
        -sMAXIMUM_MEMORY=16GB
    )
else()
    set(RHYDB_WASM_MEMORY64_LINK_OPTIONS)
endif()

target_link_options(rhydb_wasm
    PRIVATE
    -pthread
    -fexceptions
    -lembind
    ${RHYDB_WASM_MEMORY64_LINK_OPTIONS}
    -sALLOW_MEMORY_GROWTH=1
    -sEXIT_RUNTIME=0
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createRhydbModule
    # Emit TypeScript declarations (rhydb_wasm.d.ts) next to the module.
    --emit-tsd
    rhydb_wasm.d.ts
    -sEXPORTED_RUNTIME_METHODS=FS,getExceptionMessage
    -sFORCE_FILESYSTEM=1
    -sMODULARIZE=1
    -sPTHREAD_POOL_SIZE=4
    -sUSE_PTHREADS=1
    -sENVIRONMENT=web,worker,node
)
