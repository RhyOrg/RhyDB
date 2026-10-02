// Benchmarks run against real wastewater short-read data (see dataset note below):
//   * coverageGroupByOverWastewaterReads: co-occurrence groupBy over the ~141 real SARS-CoV-2
//     mutation positions in performance/mutations.csv.
//   * bareCountGroupByOverWastewaterReads: a bare `count()` group-by
// Both share a single ingested database via the RealDataMutations test fixture.
//
// The co-occurrence benchmark is the one for the HorizontalCoverageIndex coverage-scan
// optimization. That
// win only appears on *short reads with partial coverage*: each read covers a small genome window,
// so most of the 141 grouped positions are "not covered" for any given read, and the per-2^16-chunk
// coverage envelopes let the query skip the chunks that cannot cover a queried position.
//
// The input is a fixed slice of a few whole samples from the GenSpectrum W-ASAP wastewater dataset
// (the RhyDB instance behind db.wasap.genspectrum.org), in RhyDB ingest format. It is declared in
// performance/CMakeLists.txt via benchmark_dataset() and materialized under localTestData/ by the
// `benchmark_data` build target (a public, credential-free, sha256-verified download), which
// `make generateTestData` builds.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <arrow/compute/api.h>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "sequence_generator.h"
#include "rhydb/common/input_stream_wrapper.h"
#include "rhydb/config/database_config.h"
#include "rhydb/config/runtime_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/reference_genomes.h"

namespace {

using rhydb::Database;
using rhydb::config::QueryOptions;
using rhydb::query_engine::Planner;

constexpr int ITERATIONS = 5;

// Every maximal run of digits in mutations.csv is a 1-based position (e.g. "C21T" -> 21).
std::vector<uint32_t> readMutationPositions(const std::string& path) {
   std::ifstream input{path};
   if (!input) {
      throw std::runtime_error(fmt::format("could not open {}", path));
   }
   const std::string content{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
   };
   std::vector<uint32_t> positions;
   for (size_t i = 0; i < content.size();) {
      if (std::isdigit(static_cast<unsigned char>(content[i])) == 0) {
         ++i;
         continue;
      }
      uint32_t value = 0;
      while (i < content.size() && std::isdigit(static_cast<unsigned char>(content[i])) != 0) {
         value = (value * 10) + static_cast<uint32_t>(content[i] - '0');
         ++i;
      }
      positions.push_back(value);
   }
   return positions;
}

std::string buildQuery(const std::vector<uint32_t>& positions) {
   std::string assignments;
   std::string group_keys;
   for (size_t i = 0; i < positions.size(); ++i) {
      if (i > 0) {
         assignments += ", ";
         group_keys += ", ";
      }
      assignments += fmt::format("s{} := main.at({})", i, positions.at(i));
      group_keys += fmt::format("s{}", i);
   }
   return fmt::format(
      "default.map({{{}}}).groupBy({{count:=count()}}, {{{}}})", assignments, group_keys
   );
}

Database makeEmptyDatabase() {
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: real_data_mutations_benchmark
  metadata:
    - name: readId
      type: string
  primaryKey: readId
)");

   // Only the `main` nucleotide sequence is needed for the mutation query; the (real) SARS-CoV-2
   // reference is the one bundled with the example dataset, which the W-ASAP data is aligned to.
   rhydb::ReferenceGenomes reference_genomes{{{"main", readReferenceFromFile()}}, {}};

   Database database;
   database.createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         std::move(database_config),
         reference_genomes,
         {},
         rhydb::common::PhyloTree{},
         /*without_unaligned_sequences=*/true
      )
   );
   return database;
}

// `.zst` input; InputStreamWrapper decompresses on the fly, so nothing is written uncompressed to
// disk. The reads are already position-sorted, so plain (unclustered) ingestion lands same-window
// reads in the same 2^16 chunk, giving the tight per-chunk coverage envelopes the query relies on.
Database ingest() {
   Database database = makeEmptyDatabase();
   const auto dataset = benchmarkDataPath(WASAP_MUTATION_COVERAGE_ZST);
   if (!std::filesystem::exists(dataset)) {
      throw std::runtime_error(fmt::format(
         "Could not find {}. Prepare benchmark data first with `make generateTestData`.",
         dataset.string()
      ));
   }
   const rhydb::InputStreamWrapper input{dataset};

   const auto start = std::chrono::high_resolution_clock::now();
   database.appendData(rhydb::schema::TableName::getDefault(), input.getInputStream());
   const auto end = std::chrono::high_resolution_clock::now();
   SPDLOG_INFO(
      "Ingested {} in {:.2f} s",
      dataset.string(),
      std::chrono::duration<double>(end - start).count()
   );
   return database;
}

/// Plan and execute `query` through the regular Planner, returning the number of result rows.
size_t planAndExecute(
   const std::string& query,
   const Database& database,
   const QueryOptions& query_options
) {
   auto query_plan = Planner::planSaneqlQuery(query, database.tables, query_options, "bench");
   std::stringstream result;
   rhydb::query_engine::exec_node::NdjsonSink sink{&result, query_plan.results_schema};
   query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/600);

   size_t rows = 0;
   std::stringstream stream{result.str()};
   std::string line;
   while (std::getline(stream, line)) {
      if (!line.empty()) {
         ++rows;
      }
   }
   return rows;
}

/// Runs `query` ITERATIONS times, logging avg/min wall time and the result row count.
void timeQuery(
   const std::string& label,
   const std::string& query,
   const Database& database,
   const QueryOptions& query_options
) {
   double sum_ms = 0;
   double min_ms = 0;
   size_t result_rows = 0;
   for (int i = 0; i < ITERATIONS; ++i) {
      const auto start = std::chrono::high_resolution_clock::now();
      result_rows = planAndExecute(query, database, query_options);
      const auto end = std::chrono::high_resolution_clock::now();
      const double ms = std::chrono::duration<double, std::milli>(end - start).count();
      sum_ms += ms;
      min_ms = (i == 0) ? ms : std::min(min_ms, ms);
   }

   SPDLOG_INFO("[{}] Result rows: {}", label, result_rows);
   SPDLOG_INFO(
      "[{}] Query execution over {} iterations: avg {:.1f} ms, min {:.1f} ms",
      label,
      ITERATIONS,
      sum_ms / ITERATIONS,
      min_ms
   );
}

// Ingesting the wastewater dataset takes a while, so both benchmarks in this file share a single
// ingested database via the test suite fixture.
class RealDataMutations : public ::testing::Test {
  protected:
   static void SetUpTestSuite() {
      if (!database.has_value()) {
         database.emplace(ingest());
      }
   }

   static void TearDownTestSuite() { database.reset(); }

   static std::optional<Database> database;
};

std::optional<Database> RealDataMutations::database;

}  // namespace

TEST_F(RealDataMutations, coverageGroupByOverWastewaterReads) {
   const auto query_options = rhydb::config::RuntimeConfig::withDefaults().query_options;
   const auto positions = readMutationPositions("performance/mutations.csv");
   SPDLOG_INFO("Loaded {} mutation positions from performance/mutations.csv", positions.size());
   timeQuery("coverageGroupBy", buildQuery(positions), *database, query_options);
}

TEST_F(RealDataMutations, bareCountGroupByOverWastewaterReads) {
   const auto query_options = rhydb::config::RuntimeConfig::withDefaults().query_options;
   timeQuery("bareCount", "default.groupBy({n := count()})", *database, query_options);
}
