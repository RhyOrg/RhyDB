#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "rhydb/common/phylo_tree.h"
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
using rhydb::config::RuntimeConfig;
using rhydb::query_engine::Planner;

constexpr size_t NUM_INPUTS = 8;
constexpr int ITERATIONS = 5;

std::shared_ptr<Database> setupDatabase() {
   const auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: union_all_benchmark
  metadata:
    - name: primaryKey
      type: string
  primaryKey: primaryKey
)");

   const rhydb::ReferenceGenomes reference_genomes{{{"main", makeCoOccurrenceReference()}}, {}};

   auto database = std::make_shared<Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         database_config,
         reference_genomes,
         {},
         rhydb::common::PhyloTree(),
         /*without_unaligned_sequences=*/true
      )
   );

   auto ndjson = openTestDataInput(CO_OCCURRENCE_NDJSON);
   database->appendData(rhydb::schema::TableName::getDefault(), ndjson);
   return database;
}

std::string buildUnionQuery() {
   std::string query;
   for (size_t i = 0; i < NUM_INPUTS; ++i) {
      const size_t position = 1 + (i * CO_OCCURRENCE_REFERENCE_LENGTH / NUM_INPUTS);
      const std::string input = fmt::format(
         "data.filter(hasMutation(position:={}, sequenceName:='main'))"
         ".mutations(minProportion:=0.01)",
         position
      );
      query = i == 0 ? input : fmt::format("{}.unionall({})", query, input);
   }
   return query;
}

void execute(const std::shared_ptr<Database>& database, const std::string& query) {
   auto query_plan = Planner::planSaneqlQuery(
      query, database->tables, RuntimeConfig::withDefaults().query_options, "benchmark"
   );
   std::stringstream result;
   rhydb::query_engine::exec_node::NdjsonSink sink{&result, query_plan.results_schema};
   query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/600);
}

void run() {
   const auto setup_start = std::chrono::high_resolution_clock::now();
   const auto database = setupDatabase();
   const auto setup_end = std::chrono::high_resolution_clock::now();
   SPDLOG_INFO(
      "Database setup with {} sequences in {:.2f} s",
      CO_OCCURRENCE_NUM_SEQUENCES,
      std::chrono::duration<double>(setup_end - setup_start).count()
   );

   const std::string query = buildUnionQuery();
   SPDLOG_INFO("Query: {}", query);

   double sum_ms = 0;
   double min_ms = std::numeric_limits<double>::max();
   for (int i = 0; i < ITERATIONS; ++i) {
      const auto start = std::chrono::high_resolution_clock::now();
      execute(database, query);
      const auto end = std::chrono::high_resolution_clock::now();
      const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
      sum_ms += elapsed_ms;
      min_ms = std::min(min_ms, elapsed_ms);
   }
   SPDLOG_INFO(
      "unionall of {} inputs, over {} iterations: avg {:.1f} ms, min {:.1f} ms",
      NUM_INPUTS,
      ITERATIONS,
      sum_ms / ITERATIONS,
      min_ms
   );
}

}  // namespace

TEST(UnionAll, independentInputsOverSyntheticSequences) {
   run();
}
