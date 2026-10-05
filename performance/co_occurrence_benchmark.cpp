// Measurement-only benchmark for a co-occurrence query, expressed on the SaneQL interface.
//
// It builds a database of random sequences and times the end-to-end planning + execution of a
// `map({s := main.at(p)}) | group(by:={...}, aggs:={count()})` query through the regular Planner.
// There is deliberately no before/after comparison in here: run this benchmark on a branch WITHOUT
// the co-occurrence optimization to get the baseline number, and again WITH it to get the optimized
// number. Because the query is unchanged, the two runs are directly comparable.

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include <arrow/compute/api.h>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "rhydb/config/database_config.h"
#include "rhydb/config/runtime_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/reference_genomes.h"
#include "sequence_generator.h"

namespace {

using rhydb::Database;
using rhydb::config::QueryOptions;
using rhydb::query_engine::Planner;

// --- Benchmark parameters ---

// Six 1-based positions to co-group on. The dataset shape (reference length, sequence count,
// mutation rate) lives with the generator in sequence_generator.h.
constexpr std::array<size_t, 6> POSITIONS{5, 10, 20, 30, 40, 50};
constexpr int ITERATIONS = 5;

std::shared_ptr<Database> setupDatabase(const std::string& reference) {
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: co_occurrence_benchmark
  metadata:
    - name: primaryKey
      type: string
  primaryKey: primaryKey
)");

   rhydb::ReferenceGenomes reference_genomes{{{"main", reference}}, {}};

   auto database = std::make_shared<Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         std::move(database_config),
         reference_genomes,
         {},
         rhydb::common::PhyloTree{},
         /*without_unaligned_sequences=*/true
      )
   );

   auto ndjson = openTestDataInput(CO_OCCURRENCE_NDJSON);
   database->appendData(rhydb::schema::TableName::getDefault(), ndjson);
   return database;
}

std::string buildQuery() {
   std::string assignments;
   std::string group_keys;
   for (size_t i = 0; i < POSITIONS.size(); ++i) {
      if (i > 0) {
         assignments += ", ";
         group_keys += ", ";
      }
      assignments += fmt::format("s{} := main.at({})", i, POSITIONS.at(i));
      group_keys += fmt::format("s{}", i);
   }
   return fmt::format(
      "default.map({{{}}}).group(by:={{{}}}, aggs:={{count:=count()}})", assignments, group_keys
   );
}

/// Plan and execute `query` through the regular Planner, returning the number of result rows.
size_t planAndExecute(
   const std::string& query,
   const std::shared_ptr<Database>& database,
   const QueryOptions& query_options
) {
   auto query_plan = Planner::planSaneqlQuery(query, database->tables, query_options, "bench");
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

void run() {
   const auto query_options = rhydb::config::RuntimeConfig::withDefaults().query_options;

   const std::string reference = makeCoOccurrenceReference();

   SPDLOG_INFO(
      "Co-occurrence query benchmark: {} sequences, reference length {}, mutation rate {}, {} "
      "positions",
      CO_OCCURRENCE_NUM_SEQUENCES,
      CO_OCCURRENCE_REFERENCE_LENGTH,
      CO_OCCURRENCE_MUTATION_RATE,
      POSITIONS.size()
   );

   const auto setup_start = std::chrono::high_resolution_clock::now();
   auto database = setupDatabase(reference);
   const auto setup_end = std::chrono::high_resolution_clock::now();
   SPDLOG_INFO(
      "Database setup in {:.2f} s", std::chrono::duration<double>(setup_end - setup_start).count()
   );

   const std::string query = buildQuery();
   SPDLOG_INFO("Query: {}", query);

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

   SPDLOG_INFO("Result rows: {}", result_rows);
   SPDLOG_INFO(
      "Query execution over {} iterations: avg {:.1f} ms, min {:.1f} ms",
      ITERATIONS,
      sum_ms / ITERATIONS,
      min_ms
   );
}

}  // namespace

TEST(CoOccurrence, groupByOverSyntheticSequences) {
   run();
}
