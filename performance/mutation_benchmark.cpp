#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "rhydb/append/table_inserter.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"
#include "sequence_generator.h"

namespace {

using rhydb::Database;
using rhydb::query_engine::Planner;

std::shared_ptr<Database> initializeDatabaseWithSingleReference(std::string reference) {
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: test
  metadata:
    - name: key
      type: string
  primaryKey: key
)");

   rhydb::ReferenceGenomes reference_genomes{{{"main", reference}}, {}};

   auto database = std::make_shared<rhydb::Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         database_config,
         std::move(reference_genomes),
         {},
         // NOLINTNEXTLINE(readability-trailing-comma): false positive on empty brace, llvm#223646
         rhydb::common::PhyloTree{},
         /*without_unaligned_sequences=*/true
      )
   );
   return database;
}

std::shared_ptr<Database> setupTestDatabase() {
   const std::string reference = buildMutationBenchmarkReference();

   auto input_buffer = openTestDataInput(MUTATION_READS_NDJSON);

   auto database = initializeDatabaseWithSingleReference(reference);

   database->appendData(rhydb::schema::TableName::getDefault(), input_buffer);

   return database;
}

void printClipped(const std::string& output) {
   std::istringstream iss(output);
   std::string line;
   int line_count = 0;
   int total_lines = 0;

   // Count total lines
   std::istringstream counter(output);
   while (std::getline(counter, line)) {
      total_lines++;
   }

   // Print first 5 lines
   while (std::getline(iss, line) && line_count < 5) {
      SPDLOG_INFO("{}", line);
      line_count++;
   }

   if (total_lines > 5) {
      SPDLOG_INFO("... (total {} lines)", total_lines);
   }
}

void executeMutationsAllQuery(const std::shared_ptr<Database>& database) {
   auto query_plan = Planner::planSaneqlQuery(
      "data.mutations(minProportion:=0.05, sequenceNames:={main})",
      database->tables,
      {},
      "test_query"
   );
   std::stringstream result;
   rhydb::query_engine::exec_node::NdjsonSink sink{&result, query_plan.results_schema};
   query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/3);
   printClipped(result.str());
}

void executeMutationsAlmostAllQuery(const std::shared_ptr<Database>& database) {
   auto query_plan = Planner::planSaneqlQuery(
      "data.filter(!(key = '3')).mutations(minProportion:=0.05, sequenceNames:={main})",
      database->tables,
      {},
      "test_query"
   );
   std::stringstream result;
   rhydb::query_engine::exec_node::NdjsonSink sink{&result, query_plan.results_schema};
   query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/3);
   printClipped(result.str());
}
void run() {
   SPDLOG_INFO("Starting micro benchmark:");

   auto start0 = std::chrono::high_resolution_clock::now();
   auto database = setupTestDatabase();
   auto end0 = std::chrono::high_resolution_clock::now();
   auto duration0 = std::chrono::duration_cast<std::chrono::milliseconds>(end0 - start0).count();
   SPDLOG_INFO("Added all data in {}.{:03} seconds", duration0 / 1000, duration0 % 1000);

   auto start1 = std::chrono::high_resolution_clock::now();
   executeMutationsAllQuery(database);
   auto end1 = std::chrono::high_resolution_clock::now();
   auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count();
   SPDLOG_INFO(
      "Finished executeMutationsAllQuery in {}.{:03} seconds", duration1 / 1000, duration1 % 1000
   );

   auto start2 = std::chrono::high_resolution_clock::now();
   executeMutationsAlmostAllQuery(database);
   auto end2 = std::chrono::high_resolution_clock::now();
   auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count();
   SPDLOG_INFO(
      "Finished executeMutationsAlmostAllQuery in {}.{:03} seconds",
      duration2 / 1000,
      duration2 % 1000
   );
}

}  // namespace

TEST(Mutations, allAndAlmostAllQueries) {
   run();
}
