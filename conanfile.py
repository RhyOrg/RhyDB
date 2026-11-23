from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class RhyDbRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    # The `wasm` option selects the Emscripten/WebAssembly build variant. It
    # drops the native-only dependencies (gtest, mimalloc, poco) and adjusts a
    # handful of dependency options that must differ for the browser build
    # (e.g. no lzma, no threading, a slimmer Arrow). Enable it with
    # `conan install . -o "&:wasm=True"` (see buildScripts/install-wasm-dependencies).
    options = {"wasm": [True, False]}
    default_options = {"wasm": False}

    def requirements(self):
        self.requires("arrow/26.0.1")
        self.requires("boost/1.91.0")
        self.requires("fast_float/8.2.10")
        self.requires("nlohmann_json/3.12.0")
        self.requires("re2/20251105")
        self.requires("roaring/4.7.2")
        self.requires("simdjson/5.1.1")
        self.requires("simdutf/9.3.2")
        self.requires("spdlog/1.17.0")
        self.requires("yaml-cpp/0.9.0")
        self.requires("zstd/1.5.7")

        if not self.options.wasm:
            self.requires("gtest/1.18.0")
            self.requires("mimalloc/3.5.1")
            self.requires("poco/1.15.4")

    def configure(self):
        # Options shared by both the native and the WASM build.
        self.options["abseil"].shared = False

        self.options["arrow"].with_mimalloc = False
        self.options["arrow"].compute = True
        self.options["arrow"].acero = True
        self.options["arrow"].parquet = False
        self.options["arrow"].with_thrift = False
        # Registers the regex compute functions, e.g. `match_substring_regex`
        self.options["arrow"].with_re2 = True

        self.options["boost"].zstd = True
        self.options["boost"].shared = False
        self.options["boost"].without_iostreams = False
        self.options["boost"].without_serialization = False
        self.options["boost"].without_system = False
        self.options["boost"].without_random = False
        self.options["boost"].without_regex = False
        self.options["boost"].without_atomic = True
        self.options["boost"].without_chrono = True
        self.options["boost"].without_context = True
        self.options["boost"].without_contract = True
        self.options["boost"].without_coroutine = True
        self.options["boost"].without_date_time = True
        self.options["boost"].without_exception = True
        self.options["boost"].without_fiber = True
        self.options["boost"].without_filesystem = True
        self.options["boost"].without_graph = True
        self.options["boost"].without_graph_parallel = True
        self.options["boost"].without_json = True
        self.options["boost"].without_locale = True
        self.options["boost"].without_log = True
        self.options["boost"].without_math = True
        self.options["boost"].without_mpi = True
        self.options["boost"].without_nowide = True
        self.options["boost"].without_program_options = True
        self.options["boost"].without_process = True
        self.options["boost"].without_python = True
        self.options["boost"].without_stacktrace = True
        self.options["boost"].without_test = True
        self.options["boost"].without_thread = True
        self.options["boost"].without_timer = True
        self.options["boost"].without_type_erasure = True
        self.options["boost"].without_wave = True

        self.options["hwloc"].shared = False
        self.options["re2"].shared = False
        self.options["roaring"].shared = False
        self.options["simdjson"].shared = False
        self.options["simdutf"].shared = False
        self.options["spdlog"].shared = False
        self.options["yaml-cpp"].shared = False
        self.options["zstd"].shared = False

        if self.options.wasm:
            # Browser build: no lzma (not available for Emscripten), a
            # single-threaded, slimmer Arrow, and a few extra boost libraries
            # compiled out.
            self.options["arrow"].filesystem_layer = False
            self.options["arrow"].with_zlib = False

            self.options["boost"].lzma = False
            self.options["boost"].multithreading = False
            self.options["boost"].without_charconv = True
            self.options["boost"].without_cobalt = True
            self.options["boost"].without_container = True
            self.options["boost"].without_url = True
        else:
            # Native build.
            self.options["boost"].lzma = True
            self.options["boost"].without_container = False

            self.options["gtest"].no_main = True

            # this statically overrides the `malloc` symbol to use mimalloc
            self.options["mimalloc"].override = True

            self.options["poco"].shared = False
            self.options["poco"].enable_json = True
            self.options["poco"].enable_net = True
            self.options["poco"].enable_util = True
            self.options["poco"].enable_crypto = False
            self.options["poco"].enable_activerecord = False
            self.options["poco"].enable_active_record = False
            self.options["poco"].enable_data = False
            self.options["poco"].enable_data_mysql = False
            self.options["poco"].enable_data_postgresql = False
            self.options["poco"].enable_data_sqlite = False
            self.options["poco"].enable_encodings = False
            self.options["poco"].enable_jwt = False
            self.options["poco"].enable_mongodb = False
            self.options["poco"].enable_netssl = False
            self.options["poco"].enable_redis = False
            self.options["poco"].enable_xml = False
            self.options["poco"].enable_zip = False

    def generate(self):
        deps = CMakeDeps(self)
        packages = [
            "abseil",
            "boost",
            "hwloc",
            "nlohmann_json",
            "pcre2",
            "re2",
            "roaring",
            "spdlog",
            "simdjson",
            "simdutf",
            "yaml-cpp",
            "zstd",
        ]
        if not self.options.wasm:
            packages += ["gtest", "mimalloc", "poco"]
        for package in packages:
            deps.set_property(package, "cmake_find_mode", "both")
        deps.generate()

        if self.options.wasm:
            # RhyDB itself is configured with this toolchain, so it gets the same flags
            # as its dependencies
            toolchain = CMakeToolchain(self)
            # Do not write a CMakeUserPresets.json into the source tree
            toolchain.user_presets_path = False
            toolchain.generate()
