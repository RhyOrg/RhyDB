#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include <arrow/compute/api.h>
#include <emscripten/bind.h>
#include <nlohmann/json.hpp>

#include <config/source/yaml_file.h>
#include <rhydb/config/preprocessing_config.h>
#include <rhydb/database.h>
#include <rhydb/preprocessing/preprocessing.h>
#include <rhydb/query_engine/exec_node/ndjson_sink.h>
#include <rhydb/query_engine/planner.h>

namespace rhydb_wasm {

namespace {

constexpr uint64_t QUERY_TIMEOUT_SECONDS = 120;
// Native RhyDB defaults to DEFAULT_ARROW_BATCH_SIZE (32767, see
// src/rhydb/config/runtime_config.cpp) rows before it stops collecting a query
// result in memory and starts streaming it instead. Browser memory is far
// more constrained than a server process, and detail queries that project
// full nucleotide sequences can materialize large intermediate batches before
// a downstream `.limit(...)` is applied, so the browser build uses a much
// smaller cutoff.
constexpr size_t WASM_MATERIALIZATION_CUTOFF = 256;

// Not thread-safe. All embind-exported functions in this file are expected to
// be called sequentially from a single JS context (the main thread or one
// dedicated worker). Arrow's own thread pool (see PTHREAD_POOL_SIZE in
// wasm/CMakeLists.txt) is used internally during query execution but never
// calls back into these functions, so it does not introduce concurrent access
// here.
std::map<int, std::unique_ptr<rhydb::Database>> databases;
int next_database_handle = 1;

// Idempotent: safe to call before every preprocess/load/query. The static
// local is initialized exactly once (guaranteed thread-safe by the standard),
// and every subsequent call is then just a check of `initialized`.
void initializeArrowCompute() {
   static const bool initialized = [] {
      auto status = arrow::compute::Initialize();
      if (!status.ok()) {
         throw std::runtime_error(status.ToString());
      }
      return true;
   }();
   (void)initialized;
}

rhydb::config::PreprocessingConfig readPreprocessingConfig(
   const std::string& preprocessing_config_path
) {
   auto config = rhydb::config::PreprocessingConfig::withDefaults();
   const auto config_source =
      rhydb::config::YamlFile::readFile(preprocessing_config_path)
         .verify(rhydb::config::PreprocessingConfig::getConfigSpecification());
   config.overwriteFrom(config_source);
   config.validate();
   return config;
}

rhydb::config::QueryOptions browserQueryOptions() {
   return rhydb::config::QueryOptions{.materialization_cutoff = WASM_MATERIALIZATION_CUTOFF};
}

}  // namespace

int preprocess(const std::string& preprocessing_config_path) {
   initializeArrowCompute();
   auto database = std::make_unique<rhydb::Database>(
      rhydb::preprocessing::preprocessing(readPreprocessingConfig(preprocessing_config_path))
   );
   const int handle = next_database_handle++;
   databases.emplace(handle, std::move(database));
   return handle;
}

void save(int handle, const std::string& output_directory) {
   const auto database = databases.find(handle);
   if (database == databases.end()) {
      throw std::runtime_error("Unknown RhyDB database handle");
   }
   database->second->saveDatabaseState(output_directory);
}

int load(const std::string& state_directory) {
   initializeArrowCompute();
   auto database = rhydb::Database::loadDatabaseStateFromPath(state_directory);
   if (!database.has_value()) {
      throw std::runtime_error("No compatible RhyDB state found in " + state_directory);
   }
   const int handle = next_database_handle++;
   databases.emplace(handle, std::make_unique<rhydb::Database>(std::move(database.value())));
   return handle;
}

std::string query(int handle, const std::string& saneql_query) {
   initializeArrowCompute();
   const auto database = databases.find(handle);
   if (database == databases.end()) {
      throw std::runtime_error("Unknown RhyDB database handle");
   }

   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      saneql_query, database->second->tables, browserQueryOptions(), "rhydb-wasm"
   );

   std::ostringstream output_stream;
   rhydb::query_engine::exec_node::NdjsonSink output_sink{
      &output_stream, query_plan.results_schema,
   };
   query_plan.executeAndWrite(output_sink, QUERY_TIMEOUT_SECONDS);
   return output_stream.str();
}

std::string info(int handle) {
   const auto database = databases.find(handle);
   if (database == databases.end()) {
      throw std::runtime_error("Unknown RhyDB database handle");
   }
   return nlohmann::json(database->second->getDatabaseInfo()).dump();
}

void dispose(int handle) {
   databases.erase(handle);
}

EMSCRIPTEN_BINDINGS(rhydb_wasm) {
   emscripten::function("preprocess", &preprocess);
   emscripten::function("save", &save);
   emscripten::function("load", &load);
   emscripten::function("query", &query);
   emscripten::function("info", &info);
   emscripten::function("dispose", &dispose);
}

}  // namespace rhydb_wasm
