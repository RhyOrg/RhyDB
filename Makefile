## While this script is running, you will see a file `running_rhydb.flag` created in the current directory.
## It will contain the PID of the running rhydb API server.
## If something fails during execution, it might be necessary to kill the daemon using `make clean-api`.
RHYDB_DEBUG_EXECUTABLE=./build/Debug/rhydb
RHYDB_DEBUG_TEST_EXECUTABLE=./build/Debug/rhydb_test
RHYDB_DEBUG_APP_TEST_EXECUTABLE=./build/Debug/rhydb_app_test
RHYDB_RELEASE_EXECUTABLE=./build/Release/rhydb
RHYDB_RELEASE_TEST_EXECUTABLE=./build/Release/rhydb_test
RHYDB_RELEASE_APP_TEST_EXECUTABLE=./build/Release/rhydb_app_test
RHYDB_WASM_EXECUTABLE=./build/wasm/rhydb_wasm.js
RHYDB_WASM64_EXECUTABLE=./build/wasm64/rhydb_wasm.js
RHYDB_WASM_DIST_DIR=wasm/dist
RHYDB_BENCHMARK_EXECUTABLE=./build/Release/performance/rhydb_benchmark
RUNNING_RHYDB_FLAG=running_rhydb.flag
DEPENDENCIES_FLAG=dependencies
WASM_DEPENDENCIES_FLAG=build/wasm/dependencies
WASM64_DEPENDENCIES_FLAG=build/wasm64/dependencies
CLANG_FORMAT=$(shell command -v clang-format-19 2>/dev/null || command -v clang-format 2>/dev/null || echo clang-format)
CMAKE_BUILD_PARALLEL_LEVEL ?= 16
# Route cmake through `env` so recipe lines survive an emsdk-activated PATH.
# emsdk prepends a directory literally named `cmake` (its CMake module dir) to
# PATH. GNU make execs recipe lines without shell metacharacters directly via
# its own PATH search, which stops at that directory and dies with
# "cmake: Permission denied". `env` is a real binary make can exec; it redoes
# the PATH search and skips the non-executable directory. Harmless (identical
# to plain `cmake`) when emsdk is not on PATH.
CMAKE = env cmake

.PHONY: ci
ci: format all-tests

conanprofile:
	buildScripts/create-conanprofile

${DEPENDENCIES_FLAG}: conanfile.py conanprofile
	buildScripts/install-dependencies
	touch ${DEPENDENCIES_FLAG}

build/wasm/conanprofile-emscripten:
	buildScripts/create-wasm-conanprofile

${WASM_DEPENDENCIES_FLAG}: conanfile.py conanprofile build/wasm/conanprofile-emscripten
	buildScripts/install-wasm-dependencies
	touch ${WASM_DEPENDENCIES_FLAG}

build/wasm64/conanprofile-emscripten:
	WASM_ARCH=wasm64 buildScripts/create-wasm-conanprofile

# The wasm and wasm64 Conan installs write to the same shared cache (~/.conan2).
# Running them concurrently (e.g. `make -j wasm-test`) can corrupt it, so the
# wasm64 install is ordered after the wasm one via an order-only prerequisite
# (|).
${WASM64_DEPENDENCIES_FLAG}: conanfile.py conanprofile build/wasm64/conanprofile-emscripten | ${WASM_DEPENDENCIES_FLAG}
	WASM_ARCH=wasm64 buildScripts/install-wasm-dependencies
	touch ${WASM64_DEPENDENCIES_FLAG}

SRC_FILE_LIST=.src_file_list
$(SRC_FILE_LIST): FORCE
	@find src app/src wasm/src -type f | sort > $@.tmp
	@cmp -s $@ $@.tmp && rm $@.tmp || mv $@.tmp $@
.PHONY: FORCE

build/Debug/build.ninja: ${DEPENDENCIES_FLAG} $(SRC_FILE_LIST)
	$(CMAKE) -G Ninja -B build/Debug -D CMAKE_BUILD_TYPE=Debug

build/Release/build.ninja: ${DEPENDENCIES_FLAG} $(SRC_FILE_LIST)
	$(CMAKE) -G Ninja -B build/Release -D CMAKE_BUILD_TYPE=Release

build/wasm/build.ninja: ${WASM_DEPENDENCIES_FLAG} $(SRC_FILE_LIST) CMakeLists.txt wasm/CMakeLists.txt
	emcmake cmake -G Ninja -S . -B build/wasm -D CMAKE_BUILD_TYPE=Release -D BUILD_UNIT_TESTS=OFF

# --target=wasm64 must be in the initial C/C++ flags (not only in target compile
# options) so it is present while CMake configures: Emscripten's toolchain reads
# CMAKE_C_FLAGS to set CMAKE_SIZEOF_VOID_P=8 and the wasm64 library architecture.
# Without it, configure-time checks and find_package would assume a 32-bit ABI
# even though the actual compile/link is 64-bit. This mirrors the flag placement
# in the wasm64 Conan profile (see buildScripts/create-wasm-conanprofile).
build/wasm64/build.ninja: ${WASM64_DEPENDENCIES_FLAG} $(SRC_FILE_LIST) CMakeLists.txt wasm/CMakeLists.txt
	emcmake cmake -G Ninja -S . -B build/wasm64 -D CMAKE_BUILD_TYPE=Release -D BUILD_UNIT_TESTS=OFF -D RHYDB_WASM_MEMORY64=ON -D CMAKE_C_FLAGS=--target=wasm64 -D CMAKE_CXX_FLAGS=--target=wasm64

${RHYDB_DEBUG_EXECUTABLE}: build/Debug/build.ninja $(shell find src app/src -type f)
	$(CMAKE) --build build/Debug --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb

${RHYDB_DEBUG_TEST_EXECUTABLE}: build/Debug/build.ninja $(shell find src -type f)
	$(CMAKE) --build build/Debug --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_test

${RHYDB_DEBUG_APP_TEST_EXECUTABLE}: build/Debug/build.ninja $(shell find src app/src -type f)
	$(CMAKE) --build build/Debug --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_app_test

${RHYDB_RELEASE_EXECUTABLE}: build/Release/build.ninja $(shell find src app/src -type f)
	$(CMAKE) --build build/Release --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb

${RHYDB_RELEASE_TEST_EXECUTABLE}: build/Release/build.ninja $(shell find src -type f)
	$(CMAKE) --build build/Release --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_test

${RHYDB_RELEASE_APP_TEST_EXECUTABLE}: build/Release/build.ninja $(shell find src app/src -type f)
	$(CMAKE) --build build/Release --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_app_test

.PHONY: generateTestData
generateTestData: build/Release/build.ninja
	$(CMAKE) --build build/Release --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target benchmark_data

.PHONY: benchmarks
benchmarks: generateTestData
	$(CMAKE) --build build/Release --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_benchmark
	${RHYDB_BENCHMARK_EXECUTABLE}

# Only the compiled sources trigger a rebuild; wasm/CMakeLists.txt is already a
# prerequisite of build/wasm/build.ninja. Non-source assets (wasm/example,
# wasm/README.md, ...) intentionally do not force a rebuild of the binary.
${RHYDB_WASM_EXECUTABLE}: build/wasm/build.ninja $(shell find src wasm/src -type f)
	# Emscripten's --emit-tsd (see wasm/CMakeLists.txt) invokes `tsc`; make the
	# repo-local TypeScript (devDependency) discoverable on PATH for the link step.
	PATH="$(CURDIR)/node_modules/.bin:$$PATH" $(CMAKE) --build build/wasm --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_wasm

${RHYDB_WASM64_EXECUTABLE}: build/wasm64/build.ninja $(shell find src wasm/src -type f)
	# See the 32-bit rule above for why node_modules/.bin is put on PATH.
	PATH="$(CURDIR)/node_modules/.bin:$$PATH" $(CMAKE) --build build/wasm64 --parallel $(CMAKE_BUILD_PARALLEL_LEVEL) --target rhydb_wasm

.PHONY: wasm
wasm: ${RHYDB_WASM_EXECUTABLE}
	mkdir -p ${RHYDB_WASM_DIST_DIR}
	cp build/wasm/rhydb_wasm.js build/wasm/rhydb_wasm.wasm build/wasm/rhydb_wasm.d.ts ${RHYDB_WASM_DIST_DIR}/

# 64-bit (MEMORY64) variant. Requires a memory64-capable runtime (recent
# Chrome/Firefox and Node 24+; Safari does not yet support it).
.PHONY: wasm64
wasm64: ${RHYDB_WASM64_EXECUTABLE}
	mkdir -p ${RHYDB_WASM_DIST_DIR}/wasm64
	cp build/wasm64/rhydb_wasm.js build/wasm64/rhydb_wasm.wasm build/wasm64/rhydb_wasm.d.ts ${RHYDB_WASM_DIST_DIR}/wasm64/

.PHONY: wasm-test
wasm-test: wasm wasm64
	node --test wasm/test/*.test.mts

.PHONY: output
output: ${RHYDB_DEBUG_EXECUTABLE}
	export SPDLOG_LEVEL=debug; \
	${RHYDB_DEBUG_EXECUTABLE} preprocessing --database-config database_config.yaml --preprocessing-config testBaseData/test_preprocessing_config.yaml

${RUNNING_RHYDB_FLAG}: ${RHYDB_DEBUG_EXECUTABLE} output
	@{ \
		if lsof -i :8093 > /dev/null 2>&1; then \
			echo "Error: Port 8093 is already in use. Another rhydb instance might already be running."; \
			exit 1; \
		fi; \
		${RHYDB_DEBUG_EXECUTABLE} api --api-port 8093 & \
		pid=$$!; \
		echo "Waiting for rhydb (PID $$pid) to be ready..."; \
		until curl -s -o /dev/null -w "%{http_code}" http://localhost:8093/info | grep -q "200"; do \
			sleep 0.2; \
		done; \
		echo $$pid > ${RUNNING_RHYDB_FLAG}; \
		echo "rhydb is running with PID $$pid"; \
	}

.PHONY: e2e
e2e: ${RUNNING_RHYDB_FLAG}
	trap 'make clean-api' EXIT; \
	(cd endToEndTests && RHYDB_URL=localhost:8093 npm run test)

.PHONY: test
test: ${RHYDB_DEBUG_TEST_EXECUTABLE} ${RHYDB_DEBUG_APP_TEST_EXECUTABLE}
	${RHYDB_DEBUG_TEST_EXECUTABLE} --gtest_filter='*' --gtest_color=no
	${RHYDB_DEBUG_APP_TEST_EXECUTABLE} --gtest_filter='*' --gtest_color=no

.PHONY: python-tests
python-tests: ${DEPENDENCIES_FLAG}
	uv venv --allow-existing .venv
	uv pip install -q setuptools wheel 'Cython>=3.0.0'
	uv pip install -q --no-build-isolation ".[test]"
	.venv/bin/pytest python/tests -v


PYTHON_VERSIONS ?= 3.11 3.12 3.13 3.14

.PHONY: build-wheels
build-wheels: ${RHYDB_RELEASE_EXECUTABLE}
	mkdir -p wheelhouse
	@for pyversion in $(PYTHON_VERSIONS); do \
  		echo; \
		echo "--- Building wheel for Python $$pyversion ---"; \
		uv python install $$pyversion; \
		rm -rf dist; \
		if [ "$$(uname)" = "Darwin" ]; then \
			_PYTHON_HOST_PLATFORM="macosx-15.0-$$(uname -m)" uv build --wheel --python $$pyversion; \
			uv tool run --from delocate delocate-wheel -w wheelhouse/ dist/*.whl; \
		else \
			uv build --wheel --python $$pyversion; \
			uv tool run --with patchelf auditwheel repair dist/*.whl -w wheelhouse/; \
		fi; \
	done

.PHONY: all-tests
all-tests: test e2e python-tests

endToEndTests/node_modules: endToEndTests/package-lock.json
	cd endToEndTests && npm ci

.PHONY: format-cpp
format-cpp:
	find src app/src wasm/src -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs $(CLANG_FORMAT) -i

.PHONY: format-node
format-node: endToEndTests/node_modules
	cd endToEndTests && npm run format
	cd endToEndTests && npx prettier --write "../.github/workflows/*.yml"

.PHONY: format
format: format-cpp format-node

.PHONY: check-format-cpp
check-format-cpp:
	find src app/src wasm/src -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs $(CLANG_FORMAT) --dry-run --Werror

.PHONY: check-format-node
check-format-node: endToEndTests/node_modules
	cd endToEndTests && npm run check-format
	cd endToEndTests && npx prettier --check "../.github/workflows/*.yml"

.PHONY: check-format
check-format: check-format-cpp check-format-node

.PHONY: bump-serialization-version
bump-serialization-version:
	buildScripts/bump_serialization_version.sh

.PHONY: lint-changes
lint-changes:
	buildScripts/lint-changes

.PHONY: lint-all
lint-all:
	buildScripts/lint-all

.PHONY: clean-api
clean-api:
	@if [ -f ${RUNNING_RHYDB_FLAG} ]; then \
		kill $$(cat ${RUNNING_RHYDB_FLAG}) || true; \
		rm -f ${RUNNING_RHYDB_FLAG}; \
	fi

.PHONY: clean
clean: clean-api
	rm -rf output logs ${DEPENDENCIES_FLAG} ${SRC_FILE_LIST} ${RHYDB_WASM_DIST_DIR}

.PHONY: full-clean
full-clean: clean
	rm -rf build
