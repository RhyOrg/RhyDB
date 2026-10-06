#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/database_config.h"
#include "rhydb/config/runtime_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/preprocessing/lineage_definition_file.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/storage/reference_genomes.h"
#include "sequence_generator.h"

// Measures `lineage(..., includeSublineages:=true)` as it is resolved today, over lineage targets
// of varying descendant-set size and growing row counts. The column is configured the way a user
// would configure it, so the numbers always describe whichever resolution strategy is the current
// default.

namespace {

using rhydb::Database;
using rhydb::ReferenceGenomes;
using rhydb::common::LineageTreeAndIdMap;
using rhydb::common::PhyloTree;
using rhydb::config::DatabaseConfig;
using rhydb::config::RuntimeConfig;
using rhydb::initialize::Initializer;
using rhydb::query_engine::Planner;
using rhydb::schema::TableName;

const std::filesystem::path LINEAGE_DEFINITION_PATH =
   "testBaseData/exampleDataset/lineage_definition.yaml";
// The `generateLineageIndex` value; must match the key of the lineage tree passed to the
// initializer.
const std::string LINEAGE_TREE_NAME = "lineage_definition.yaml";

std::string config() {
   return fmt::format(
      R"(
schema:
  instanceName: benchmark
  metadata:
    - name: primaryKey
      type: string
    - name: pango_lineage
      type: string
      generateIndex: true
      generateLineageIndex: {}
  primaryKey: primaryKey
)",
      LINEAGE_TREE_NAME
   );
}

std::vector<std::string> loadLineageNames() {
   auto file = rhydb::preprocessing::LineageDefinitionFile::fromYAMLFile(LINEAGE_DEFINITION_PATH);
   std::vector<std::string> names;
   names.reserve(file.lineages.size());
   for (const auto& lineage : file.lineages) {
      names.push_back(lineage.lineage_name.string);
   }
   return names;
}

std::string generateNdjson(const std::vector<std::string>& names, size_t num_rows, uint32_t seed) {
   std::mt19937 rng(seed);
   std::uniform_int_distribution<size_t> dist(0, names.size() - 1);
   std::string out;
   for (size_t i = 0; i < num_rows; ++i) {
      const nlohmann::json line{
         {"primaryKey", fmt::format("id_{}", i)}, {"pango_lineage", names.at(dist(rng))}
      };
      out += line.dump();
      out += '\n';
   }
   return out;
}

std::shared_ptr<Database> buildDatabase(
   const std::string& ndjson,
   const std::map<std::filesystem::path, LineageTreeAndIdMap>& lineage_trees
) {
   auto database = std::make_shared<Database>();
   Initializer::createTableInDatabase(
      TableName::getDefault(),
      DatabaseConfig::getValidatedConfig(config()),
      ReferenceGenomes{{}, {}},
      lineage_trees,
      PhyloTree{},
      /*without_unaligned_sequences=*/true,
      *database
   );
   std::stringstream ndjson_stream{ndjson};
   std::istream input_stream(ndjson_stream.rdbuf());
   database->appendData(TableName::getDefault(), input_stream);
   return database;
}

std::string countQuery(const std::string& lineage, std::string_view recombinant_mode) {
   return fmt::format(
      "data.filter(pango_lineage.lineage('{}', includeSublineages:=true, "
      "recombinantFollowingMode:='{}')).group(by:={{}}, aggs:={{count := count()}})",
      lineage,
      recombinant_mode
   );
}

int64_t executeCount(const std::shared_ptr<Database>& database, const std::string& query) {
   auto query_plan = Planner::planSaneqlQuery(
      query, database->tables, RuntimeConfig::withDefaults().query_options, "benchmark"
   );
   std::stringstream result;
   rhydb::query_engine::exec_node::NdjsonSink sink{&result, query_plan.results_schema};
   query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/300);
   const std::string line = result.str();
   if (line.empty()) {
      return 0;
   }
   return nlohmann::json::parse(line).at("count").get<int64_t>();
}

struct Timing {
   double avg_ms;
   double min_ms;
};

Timing timeQuery(
   const std::shared_ptr<Database>& database,
   const std::string& query,
   int iterations
) {
   double sum_ms = 0;
   double min_ms = std::numeric_limits<double>::max();
   for (int i = 0; i < iterations; ++i) {
      const auto start = std::chrono::high_resolution_clock::now();
      executeCount(database, query);
      const auto end = std::chrono::high_resolution_clock::now();
      const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
      sum_ms += elapsed_ms;
      min_ms = std::min(min_ms, elapsed_ms);
   }
   return {.avg_ms = sum_ms / iterations, .min_ms = min_ms};
}

void run() {
   SPDLOG_INFO("=== Lineage filter ===");

   const auto lineage_tree =
      LineageTreeAndIdMap::fromLineageDefinitionFilePath(LINEAGE_DEFINITION_PATH);
   const std::map<std::filesystem::path, LineageTreeAndIdMap> lineage_trees{
      {LINEAGE_TREE_NAME, lineage_tree}
   };
   const std::vector<std::string> names = loadLineageNames();
   SPDLOG_INFO("Loaded {} lineages from {}", names.size(), LINEAGE_DEFINITION_PATH.string());

   // B.1 has >=2983 descendants, B.1.617.2 (Delta) >=244, and B.1.351.2 is (and should stay) a leaf
   const std::vector<std::pair<std::string, std::string>> targets{
      {"near-root", "B.1"},
      {"mid-level", "B.1.617.2"},
      {"leaf", "B.1.351.2"},
   };

   constexpr int ITERATIONS = 5;
   const std::vector<size_t> row_counts{10'000, 100'000, 500'000};

   for (const size_t num_rows : row_counts) {
      SPDLOG_INFO("");
      SPDLOG_INFO("--- {} rows ---", num_rows);
      const std::string ndjson = generateNdjson(names, num_rows, /*seed=*/42);

      const auto start_build = std::chrono::high_resolution_clock::now();
      const auto database = buildDatabase(ndjson, lineage_trees);
      const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::high_resolution_clock::now() - start_build
      )
                               .count();

      SPDLOG_INFO("build: {} ms", build_ms);

      for (const auto& [target_label, target] : targets) {
         for (const std::string_view mode :
              {"doNotFollow", "followIfFullyContainedInClade", "alwaysFollow"}) {
            const std::string query = countQuery(target, mode);
            const int64_t matches = executeCount(database, query);
            const Timing timing = timeQuery(database, query, ITERATIONS);

            SPDLOG_INFO(
               "{:<14} '{}' [{:<30}] matches={:<7} | avg={:.3f}ms min={:.3f}ms",
               target_label,
               target,
               mode,
               matches,
               timing.avg_ms,
               timing.min_ms
            );
         }
      }
   }

   SPDLOG_INFO("");
   SPDLOG_INFO("=== Benchmark Complete ===");
}

}  // namespace

TEST(LineageFilter, includeSublineagesOverGrowingRowCounts) {
   run();
}
