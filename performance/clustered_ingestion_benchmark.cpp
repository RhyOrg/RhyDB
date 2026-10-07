#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arrow/compute/initialize.h>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "rhydb/append/table_inserter.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"
#include "sequence_generator.h"

// Demonstrates what N-way clustered ingestion buffering buys on amplicon-coverage short reads.
//
// The reads are drawn from a fixed set of primer-defined amplicon windows (see
// generate_test_data / writeAmpliconShortReadNdjson), so a nucleotide-position filter only matches
// reads of the one amplicon covering that position. When those reads land contiguously in the
// ingested chunks the coverage filter can skip whole chunks, so the query set runs fast; when they
// are scattered across every chunk it cannot, so it runs slow.
//
// This benchmark builds the database three ways and times the identical 10k-query set against each:
//   1. amplicon-sorted input, clustering off   -- the ideal layout, for free
//   2. amplicon-shuffled input, clustering off  -- worst case, coverage scattered everywhere
//   3. amplicon-shuffled input, clustering on   -- clustered buffering reorders it back
// The point of the feature is that (3) recovers the query performance of (1) from the same
// scattered input as (2). Running all three in one binary keeps the comparison self-contained; no
// environment variables or rebuilds are needed to switch between them.

using rhydb::Database;
using rhydb::query_engine::Planner;

namespace {

constexpr size_t DEFAULT_QUERY_COUNT = 10'000;
constexpr size_t CLUSTER_NUM_BUFFERS = 128;
constexpr double CLUSTER_SPAN_GROWTH_THRESHOLD = 0.01;

rhydb::append::ClusteredBufferingOptions clusteredOptions() {
   rhydb::append::ClusteredBufferingOptions options;
   options.enabled = true;
   options.driver_column_name = "main";
   options.num_buffers = CLUSTER_NUM_BUFFERS;
   options.span_growth_threshold_fraction = CLUSTER_SPAN_GROWTH_THRESHOLD;
   return options;
}

struct Scenario {
   std::string_view name;
   std::string_view dataset_path;
   rhydb::append::ClusteredBufferingOptions clustering;
};

// Builds a fresh database from the scenario's dataset and returns how long ingestion took.
std::pair<std::shared_ptr<Database>, double> buildDatabase(
   const std::string& reference,
   const Scenario& scenario
) {
   auto input_file = openTestDataInput(scenario.dataset_path);
   SPDLOG_INFO("Reading short read NDJSON data from {}", scenario.dataset_path);

   auto database = initializeDatabaseWithShortReadSchema(reference);
   const auto start = std::chrono::high_resolution_clock::now();
   database->appendData(rhydb::schema::TableName::getDefault(), input_file, scenario.clustering);
   const auto end = std::chrono::high_resolution_clock::now();
   const double seconds = std::chrono::duration<double>(end - start).count();
   return {database, seconds};
}

class QueryGenerator {
   std::mt19937 rng;
   size_t reference_length;
   size_t query_counter = 0;
   static constexpr std::array<char, 5> SYMBOLS = {'A', 'C', 'G', 'T', '-'};

  public:
   explicit QueryGenerator(size_t ref_length, uint64_t seed = 42)
       : rng(seed),
         reference_length(ref_length) {}

   std::string generateQuery() {
      std::uniform_int_distribution<size_t> pos_dist(1, reference_length - 1);
      const size_t position = pos_dist(rng);

      const bool use_all_symbols = (query_counter++ % 2 == 1);

      if (use_all_symbols) {
         return fmt::format(
            "data.filter("
            "locationName = 'generated' && "
            "samplingDate.between('2024-01-01'::date, '2024-01-07'::date) && "
            "(nucleotideEquals(position:={0}, symbol:='A', sequenceName:='main') || "
            "nucleotideEquals(position:={0}, symbol:='C', sequenceName:='main') || "
            "nucleotideEquals(position:={0}, symbol:='G', sequenceName:='main') || "
            "nucleotideEquals(position:={0}, symbol:='T', sequenceName:='main') || "
            "nucleotideEquals(position:={0}, symbol:='-', sequenceName:='main')) && "
            "samplingDate.between('2024-01-01'::date, '2024-01-07'::date)"
            ").group(by:={{}}, aggs:={{count:=count()}})",
            position
         );
      }
      std::uniform_int_distribution<size_t> sym_dist(0, SYMBOLS.size() - 1);
      const char symbol = SYMBOLS[sym_dist(rng)];
      return fmt::format(
         "data.filter("
         "locationName = 'generated' && "
         "samplingDate.between('2024-01-01'::date, '2024-01-07'::date) && "
         "nucleotideEquals(position:={}, symbol:='{}', sequenceName:='main') && "
         "samplingDate.between('2024-01-01'::date, '2024-01-07'::date)"
         ").group(by:={{}}, aggs:={{count:=count()}})",
         position,
         symbol
      );
   }
};

// Runs the fixed query set against the database and returns how long it took.
double executeAllQueries(
   const std::shared_ptr<Database>& database,
   size_t reference_length,
   size_t query_count = DEFAULT_QUERY_COUNT
) {
   QueryGenerator query_gen(reference_length);
   const auto start = std::chrono::high_resolution_clock::now();
   for (size_t query_num = 1; query_num <= query_count; ++query_num) {
      if (query_num % 1000 == 0) {
         SPDLOG_INFO("Executing query number {}", query_num);
      }
      std::string query_string = query_gen.generateQuery();
      auto query_plan = Planner::planSaneqlQuery(query_string, database->tables, {}, "test_query");

      std::ofstream null_output("/dev/null");
      rhydb::query_engine::exec_node::NdjsonSink sink{&null_output, query_plan.results_schema};
      query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/20);
   }
   const auto end = std::chrono::high_resolution_clock::now();
   return std::chrono::duration<double>(end - start).count();
}

struct ScenarioResult {
   std::string_view name;
   double ingest_seconds;
   double query_seconds;
};

void run() {
   const std::string reference = readReferenceFromFile();
   SPDLOG_INFO("Read reference sequence of length {}", reference.size());

   const std::array<Scenario, 3> scenarios{
      {
         {
            .name = "amplicon-sorted, ingestion clustering off",
            .dataset_path = SHORT_READ_AMPLICON_SORTED_NDJSON,
            .clustering = {},
         },
         {
            .name = "amplicon-shuffled, ingestion clustering off",
            .dataset_path = SHORT_READ_AMPLICON_SHUFFLED_NDJSON,
            .clustering = {},
         },
         {
            .name = "amplicon-shuffled, 128-way clustered ingestion",
            .dataset_path = SHORT_READ_AMPLICON_SHUFFLED_NDJSON,
            .clustering = clusteredOptions(),
         },
      },
   };

   std::vector<ScenarioResult> results;
   for (const auto& scenario : scenarios) {
      SPDLOG_INFO("=== Scenario: {} ===", scenario.name);
      auto [database, ingest_seconds] = buildDatabase(reference, scenario);
      SPDLOG_INFO(
         "Ingested in {:.3f} seconds; running {} queries", ingest_seconds, DEFAULT_QUERY_COUNT
      );
      const double query_seconds = executeAllQueries(database, reference.size());
      SPDLOG_INFO("Finished query set in {:.3f} seconds", query_seconds);
      results.push_back(
         {.name = scenario.name, .ingest_seconds = ingest_seconds, .query_seconds = query_seconds}
      );
   }

   SPDLOG_INFO("=== Summary (ingestion / {} queries) ===", DEFAULT_QUERY_COUNT);
   for (const auto& result : results) {
      SPDLOG_INFO(
         "{:<48} ingest {:>8.3f}s   queries {:>8.3f}s",
         result.name,
         result.ingest_seconds,
         result.query_seconds
      );
   }
}

}  // namespace

TEST(ClusteredIngestion, ampliconLayoutsWithAndWithoutClustering) {
   run();
}
