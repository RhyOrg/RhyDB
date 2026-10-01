#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <arrow/compute/initialize.h>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "sequence_generator.h"
#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"

using rhydb::Database;
using rhydb::query_engine::Planner;

namespace {

// ---- Benchmark infrastructure ----

struct BenchmarkResult {
   double avg_ms;
   double min_ms;
   double max_ms;
};

BenchmarkResult runBenchmark(
   const std::shared_ptr<Database>& database,
   const std::string& query_str,
   int iterations
) {
   std::vector<int64_t> durations;
   durations.reserve(iterations);

   const auto query_options = rhydb::config::RuntimeConfig::withDefaults().query_options;
   for (int i = 0; i < iterations; ++i) {
      // rewrite() and compile() — including the full NOf DP pass — happen inside
      // planSaneqlQuery, so the timer must start before it.
      const auto start = std::chrono::high_resolution_clock::now();
      auto query_plan =
         Planner::planSaneqlQuery(query_str, database->tables, query_options, "benchmark_query");
      std::ofstream null_output("/dev/null");
      rhydb::query_engine::exec_node::NdjsonSink sink{&null_output, query_plan.results_schema};
      query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/60);
      const auto end = std::chrono::high_resolution_clock::now();
      durations.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
      );
   }

   const int64_t sum = std::accumulate(durations.begin(), durations.end(), int64_t{0});
   const int64_t min_val = std::ranges::min(durations);
   const int64_t max_val = std::ranges::max(durations);

   return BenchmarkResult{
      .avg_ms = static_cast<double>(sum) / static_cast<double>(iterations) / 1000.0,
      .min_ms = static_cast<double>(min_val) / 1000.0,
      .max_ms = static_cast<double>(max_val) / 1000.0,
   };
}

// ---- Query construction ----

// NucleotideMutationProfile with querySequence rewrites to
//   Not(N-Of(SymbolInSet children, distance+1, false))
// with one child per non-N position in query_sequence (~genome_length children for a full
// sequence).  This exercises the single-pass NOf optimisation at large scale.
std::string buildMutationProfileQuery(const std::string& query_sequence, uint32_t distance) {
   return fmt::format(
      "default.filter(nucleotideMutationProfile(distance:={}, sequenceName:='main', "
      "querySequence:='{}'))"
      ".group({{count:=count()}})",
      distance,
      query_sequence
   );
}

// ---- Database setup ----

std::shared_ptr<Database> loadShortReadDatabase(
   const std::string& reference,
   std::string_view path
) {
   SPDLOG_INFO("Loading short reads from {}...", path);
   auto ndjson = openTestDataInput(path);
   auto database = initializeDatabaseWithShortReadSchema(reference);
   database->appendData(rhydb::schema::TableName::getDefault(), ndjson);
   SPDLOG_INFO("Short-read database ready.");
   return database;
}

std::shared_ptr<Database> loadFullSequenceDatabase(
   const std::string& reference,
   std::string_view path
) {
   SPDLOG_INFO("Loading full-length sequences from {}...", path);
   auto ndjson = openTestDataInput(path);
   auto database = initializeDatabaseWithFullSequenceSchema(reference);
   database->appendData(rhydb::schema::TableName::getDefault(), ndjson);
   SPDLOG_INFO("Full-sequence database ready.");
   return database;
}

// ---- Main benchmark runner ----

void runMutationProfileBenchmarks(
   const std::string& label,
   const std::shared_ptr<Database>& database,
   const std::string& query_sequence,
   const std::vector<uint32_t>& distances,
   int iterations
) {
   SPDLOG_INFO("=== {} ===", label);
   SPDLOG_INFO(
      "  Query sequence length: {} (generates ~{} N-Of children)",
      query_sequence.size(),
      query_sequence.size()
   );

   for (const uint32_t distance : distances) {
      const auto query = buildMutationProfileQuery(query_sequence, distance);
      const auto result = runBenchmark(database, query, iterations);
      SPDLOG_INFO(
         "  MutationProfile(distance={:>4}): avg={:.2f}ms  min={:.2f}ms  max={:.2f}ms",
         distance,
         result.avg_ms,
         result.min_ms,
         result.max_ms
      );
   }

   SPDLOG_INFO("");
}

void run() {
   const std::string reference = readReferenceFromFile();
   SPDLOG_INFO("Reference genome length: {}", reference.size());
   SPDLOG_INFO("");

   // Generate an evolved sequence to use as the query profile.
   // Using a leaf of the tree maximises divergence from the reference (~5 * genome_length *
   // mutation_rate mutations), giving a realistic large NOf with many non-trivial children.
   SequenceTreeGenerator tree_gen(reference);
   const auto evolved = tree_gen.generateEvolvedSequences();
   const std::string& query_sequence = evolved.back();
   SPDLOG_INFO(
      "Using evolved sequence as query profile ({} sequences generated, using last)", evolved.size()
   );
   SPDLOG_INFO("");

   const auto short_read_db = loadShortReadDatabase(reference, SHORT_READ_SMALL_NDJSON);
   const auto short_read_db_large = loadShortReadDatabase(reference, SHORT_READ_LARGE_NDJSON);
   const auto full_seq_db = loadFullSequenceDatabase(reference, FULL_SEQUENCE_NDJSON);

   // distance=0 tests the "almost nothing matches" extreme (exact profile match).
   // Large distances test the "almost everything matches" extreme.
   const std::vector<uint32_t> distances = {0, 5, 50, 200};
   constexpr int ITERATIONS = 10;

   SPDLOG_INFO("Running MutationProfile benchmarks ({} iterations per case)", ITERATIONS);
   SPDLOG_INFO("");

   runMutationProfileBenchmarks(
      "Short-read database", short_read_db, query_sequence, distances, ITERATIONS
   );
   runMutationProfileBenchmarks(
      "Large short-read database", short_read_db_large, query_sequence, distances, ITERATIONS
   );
   runMutationProfileBenchmarks(
      "Full-sequence database", full_seq_db, query_sequence, distances, ITERATIONS
   );

   SPDLOG_INFO("=== Benchmark complete ===");
}

}  // namespace

TEST(NOfSequenceFilter, distancesAndMutationProfiles) {
   run();
}
