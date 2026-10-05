# AGENTS.md - Developer Guide for RhyDB

C++23 high-performance genomic sequence indexing engine. Uses Arrow Acero for query execution, roaring bitmaps for filtering, SaneQL as query language.

**Key Technologies:** C++23, CMake, Conan, GTest, spdlog, Arrow Acero, Boost, simdjson

Documentation (for devs and users) in `documentation/`.

## Build & Test

```bash
# Build
make build/Debug/rhydb             # Debug build (includes ASAN)
make build/Release/rhydb           # Release build (includes mimalloc on Linux)

make clean                         # Clean generated outputs/logs/deps (keeps build/)
make full-clean                    # clean + remove build/ (forces full reconfigure + rebuild)

# Test
make test                          # Build and run C++ unit tests
make all-tests                     # Run C++, Python, and E2E tests

# WebAssembly (browser) build (see wasm/README.md)
make wasm                          # Build rhydb_wasm.js/.wasm into wasm/dist (requires Emscripten)
make wasm-test                     # Build wasm and run wasm/test

# Run specific tests
build/Debug/rhydb_test --gtest_filter='TestSuite*'         # By suite
build/Debug/rhydb_test --gtest_filter='TestSuite.testCase'  # By case
build/Debug/rhydb_test --gtest_filter='*Pattern*'           # By pattern

# Format & CI
make format                        # Format C++ code (requires clang-format)
make ci                            # Format + run all tests
```

CMake auto-scans `src/` and `app/src/` directories. The Makefile maintains `.src_file_list` to retrigger CMake when files are added/removed, so no CMakeLists.txt changes (or clean rebuild) are required.

## Code Style

### Naming (enforced by clang-tidy)
- **Files:** `snake_case.cpp`, `snake_case.h`
- **Namespaces:** `lower_case`
- **Classes/Structs:** `CamelCase`
- **Functions/Methods:** `camelBack`
- **Variables/Members:** `lower_case`
- **Constants/Enums:** `UPPER_CASE`

### Formatting (clang-format 19)
- 3-space indent, 100-char column limit, Chromium brace style, `#pragma once`
- Trailing commas: add a trailing comma after the last element in multi-line braced initializers / aggregate initializations (keeps diffs minimal when appending fields).

### Include order
```cpp
#include "rhydb/my_module/my_file.h"     // 1. Corresponding header

#include <string>                        // 2. System (standard library)

#include <arrow/acero/exec_plan.h>       // 3. External (angle brackets)

#include "rhydb/common/panic.h"           // 4. Internal (quotes)
```

### `using` statements

Never use `using my::Type` in `.h` files.
Use `using my::Type` (only in `.cpp` files) deliberately for all `rhydb::...` types.

### Error handling
- `std::expected<T, Error>` for recoverable errors.
- `RHYDB_ASSERT()` for debug assertions.
- Arrow functions: use `ARROW_ASSIGN_OR_RAISE` / `ARROW_RETURN_NOT_OK` macros.
- `CHECK_RHYDB_QUERY(condition, fmt, args...)` for query validation errors shown to users.
- Exceptions only for preprocessing/initialization.

### Logging
Use spdlog macros: `SPDLOG_ERROR`, `SPDLOG_INFO`, `SPDLOG_DEBUG`, `SPDLOG_TRACE`.
Runtime level controlled by `SPDLOG_LEVEL` env var (`trace`, `debug`, `info`, `warn`, `error`, `off`).

## Commit Messages

[Conventional Commits](https://www.conventionalcommits.org/). Should reference an issue or PR when possible.

```
feat: add unionAll operator

resolves #1221
```

**Types:** `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

**Breaking changes:** add a `BREAKING CHANGE:` footer describing the impact and migration. release-please shows the footer text verbatim in the changelog. Since PRs are squash-merged, put the marker in the PR title and the footer at the bottom of the PR description.

```
feat(query_engine): disallow `... = null` filters

resolves #1503

BREAKING CHANGE: `column = null` filters now error. Use `column.isNull()` instead.
```
