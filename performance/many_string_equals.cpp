#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include "rhydb/append/table_inserter.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/scalar_expressions/comparison.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/or.h"
#include "rhydb/query_engine/scalar_expressions/string_in_set.h"
#include "sequence_generator.h"

namespace {

using rhydb::Database;
using rhydb::query_engine::Planner;
using rhydb::query_engine::operators::AggregateDefinition;
using rhydb::query_engine::operators::AggregateFunction;
using rhydb::query_engine::operators::AggregateNode;
using rhydb::query_engine::operators::FilterNode;
using rhydb::query_engine::operators::TableScanNode;
using rhydb::query_engine::scalar_expressions::BoolLiteral;
using rhydb::query_engine::scalar_expressions::Comparison;
using rhydb::query_engine::scalar_expressions::FieldRef;
using rhydb::query_engine::scalar_expressions::Or;
using rhydb::query_engine::scalar_expressions::ScalarExpression;
using rhydb::query_engine::scalar_expressions::ScalarExpressionVector;
using rhydb::query_engine::scalar_expressions::StringInSet;
using rhydb::query_engine::scalar_expressions::StringLiteral;

/// Build an equality predicate on an indexed string column, i.e. `column = value`.
std::unique_ptr<ScalarExpression> makeStringEquals(
   const std::string& column,
   const std::string& value
) {
   return std::make_unique<Comparison>(
      std::make_unique<FieldRef>(rhydb::schema::ColumnIdentifier{
         .name = column, .type = rhydb::schema::ColumnType::DICTIONARY_ENCODED
      }),
      std::make_unique<StringLiteral>(value),
      rhydb::query_engine::filter::operators::Comparator::EQUALS
   );
}

std::shared_ptr<Database> initializeDatabase() {
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: test
  metadata:
    - name: accession
      type: string
    - name: country
      type: string
      generateIndex: true
  primaryKey: accession
)");

   rhydb::ReferenceGenomes reference_genomes{{}, {}};

   auto database = std::make_shared<rhydb::Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         database_config,
         std::move(reference_genomes),
         {},
         rhydb::common::PhyloTree{},
         /*without_unaligned_sequences=*/true
      )
   );
   return database;
}

/// Create a database from the generated accession/country records on disk
std::shared_ptr<Database> setupTestDatabase() {
   auto input_buffer = openTestDataInput(STRING_EQUALS_NDJSON);
   auto database = initializeDatabase();
   database->appendData(rhydb::schema::TableName::getDefault(), input_buffer);

   return database;
}

/// Build an OR expression with many StringEquals clauses
std::unique_ptr<ScalarExpression> buildManyStringEquals(
   const std::string& column,
   const std::vector<std::string>& values
) {
   ScalarExpressionVector children;
   children.reserve(values.size());
   for (const auto& value : values) {
      children.push_back(makeStringEquals(column, value));
   }
   return std::make_unique<Or>(std::move(children));
}

/// Build an OR expression with many StringEquals clauses
std::unique_ptr<ScalarExpression> buildManyNestedStringEquals(
   const std::string& column,
   const std::vector<std::string>& values
) {
   RHYDB_ASSERT(values.size() >= 2);
   ScalarExpressionVector children;
   children.push_back(makeStringEquals(column, values.at(0)));
   children.push_back(makeStringEquals(column, values.at(1)));
   std::unique_ptr<Or> result = std::make_unique<Or>(std::move(children));
   for (size_t idx = 2; idx < values.size(); ++idx) {
      children = ScalarExpressionVector{};
      children.push_back(makeStringEquals(column, values.at(idx)));
      children.push_back(std::move(result));
      result = std::make_unique<Or>(std::move(children));
   }
   return result;
}

/// Build a StringInSet expression
std::unique_ptr<ScalarExpression> buildStringInSet(
   const std::string& column,
   const std::vector<std::string>& values
) {
   const rhydb::schema::ColumnIdentifier column_identifier{
      .name = column, .type = rhydb::schema::ColumnType::STRING
   };
   std::unordered_set<std::string> value_set(values.begin(), values.end());
   return std::make_unique<StringInSet>(column_identifier, std::move(value_set));
}

void executeCountWithFilter(
   const std::shared_ptr<Database>& database,
   std::unique_ptr<ScalarExpression> filter
) {
   const auto& table_name = rhydb::schema::TableName::getDefault();
   auto table = database->tables.at(table_name);
   auto scan = std::make_unique<TableScanNode>(
      table, std::make_unique<BoolLiteral>(true), table->schema->getColumnIdentifiers()
   );
   auto filter_node = std::make_unique<FilterNode>(std::move(scan), std::move(filter));
   std::vector<AggregateDefinition> aggregates{
      {.output_name = "count", .function = AggregateFunction::COUNT, .source_column = std::nullopt}
   };
   auto root = std::make_unique<AggregateNode>(
      std::move(filter_node), std::vector<rhydb::schema::ColumnIdentifier>{}, std::move(aggregates)
   );
   auto query_plan = Planner::planQuery(std::move(root), database->tables, {}, "benchmark_query");
   std::stringstream result;
   rhydb::query_engine::exec_node::NdjsonSink sink{&result, query_plan.results_schema};
   query_plan.executeAndWrite(sink, /*timeout_in_seconds=*/60);
}

struct BenchmarkResult {
   double avg_ms;
   double min_ms;
   double max_ms;
};

template <typename FilterBuilder>
BenchmarkResult runBenchmark(
   const std::shared_ptr<Database>& database,
   FilterBuilder build_filter,
   int iterations
) {
   std::vector<int64_t> durations;
   durations.reserve(iterations);

   for (int i = 0; i < iterations; ++i) {
      // Build a fresh filter for each iteration
      auto filter = build_filter();

      auto start = std::chrono::high_resolution_clock::now();
      executeCountWithFilter(database, std::move(filter));
      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      durations.push_back(duration);
   }

   // Calculate statistics
   int64_t sum = 0;
   int64_t min_val = durations[0];
   int64_t max_val = durations[0];
   for (const int64_t duration : durations) {
      sum += duration;
      min_val = std::min(min_val, duration);
      max_val = std::max(max_val, duration);
   }

   return BenchmarkResult{
      .avg_ms = static_cast<double>(sum) / static_cast<double>(iterations) / 1000.0,
      .min_ms = static_cast<double>(min_val) / 1000.0,
      .max_ms = static_cast<double>(max_val) / 1000.0
   };
}

void run() {
   SPDLOG_INFO("=== StringInSet vs Many StringEquals Performance Benchmark ===");
   SPDLOG_INFO("");

   constexpr size_t NUM_RECORDS = STRING_EQUALS_RECORD_COUNT;
   constexpr int ITERATIONS = 5;

   SPDLOG_INFO("Setting up test database with {} records...", NUM_RECORDS);
   auto start_setup = std::chrono::high_resolution_clock::now();
   auto database = setupTestDatabase();
   auto end_setup = std::chrono::high_resolution_clock::now();
   auto setup_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_setup - start_setup).count();
   SPDLOG_INFO(
      "Database setup completed in {}.{:03} seconds", setup_duration / 1000, setup_duration % 1000
   );
   SPDLOG_INFO("");

   std::vector<size_t> test_sizes = {10, 100, 500, 1000, 5000};

   for (size_t num_values : test_sizes) {
      SPDLOG_INFO("--- Benchmark with {} search values ---", num_values);

      // Generate search values (mix of existing and non-existing)
      std::vector<std::string> search_values;
      search_values.reserve(num_values);
      for (size_t i = 0; i < num_values; ++i) {
         // Every 10th value doesn't exist in the database
         if (i % 10 == 9) {
            search_values.push_back(fmt::format("NOTEXIST{:06}", i));
         } else {
            // Pick values spread across the database
            size_t idx = (i * NUM_RECORDS / num_values) % NUM_RECORDS;
            search_values.push_back(fmt::format("ACC{:06}", idx));
         }
      }

      // Lambda builders that capture search_values
      auto build_or = [&] { return buildManyStringEquals("accession", search_values); };
      auto build_nested_or = [&] {
         return buildManyNestedStringEquals("accession", search_values);
      };
      auto build_set = [&] { return buildStringInSet("accession", search_values); };

      // Run benchmarks
      auto or_result = runBenchmark(database, build_or, ITERATIONS);
      SPDLOG_INFO(
         "OR({} StringEquals): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
         num_values,
         or_result.avg_ms,
         or_result.min_ms,
         or_result.max_ms
      );

      // Run benchmarks
      auto nested_or_result = runBenchmark(database, build_nested_or, ITERATIONS);
      SPDLOG_INFO(
         "OR(StringEquals, OR(..{}x)): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
         num_values,
         nested_or_result.avg_ms,
         nested_or_result.min_ms,
         nested_or_result.max_ms
      );

      auto set_result = runBenchmark(database, build_set, ITERATIONS);
      SPDLOG_INFO(
         "StringInSet({}):     avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
         num_values,
         set_result.avg_ms,
         set_result.min_ms,
         set_result.max_ms
      );

      SPDLOG_INFO("");
   }

   // Test on indexed column (country)
   SPDLOG_INFO("=== Testing on INDEXED column (country) ===");
   SPDLOG_INFO("");

   const std::vector<std::string> all_countries(
      STRING_EQUALS_COUNTRIES.begin(), STRING_EQUALS_COUNTRIES.end()
   );

   for (size_t num_values : test_sizes) {
      SPDLOG_INFO("--- Benchmark with {} country values (indexed column) ---", num_values);

      std::vector<std::string> search_countries;
      search_countries.reserve(num_values);
      for (size_t i = 0; i < num_values; ++i) {
         // Every 11th value takes a country from the list
         if (i % 11 == 0) {
            size_t idx = i % all_countries.size();
            search_countries.push_back(all_countries.at(idx));
         } else {
            // Pick values spread across the database
            search_countries.push_back(fmt::format("NOTEXIST{:06}", i));
         }
      }
      auto build_or = [&] { return buildManyStringEquals("country", search_countries); };
      auto build_nested_or = [&] {
         return buildManyNestedStringEquals("country", search_countries);
      };
      auto build_set = [&] { return buildStringInSet("country", search_countries); };

      auto or_result = runBenchmark(database, build_or, ITERATIONS);
      SPDLOG_INFO(
         "OR({} StringEquals): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
         num_values,
         or_result.avg_ms,
         or_result.min_ms,
         or_result.max_ms
      );

      // Run benchmarks
      auto nested_or_result = runBenchmark(database, build_nested_or, ITERATIONS);
      SPDLOG_INFO(
         "OR(StringEquals, OR(..{}x)): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
         num_values,
         nested_or_result.avg_ms,
         nested_or_result.min_ms,
         nested_or_result.max_ms
      );

      auto set_result = runBenchmark(database, build_set, ITERATIONS);
      SPDLOG_INFO(
         "StringInSet({}):     avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
         num_values,
         set_result.avg_ms,
         set_result.min_ms,
         set_result.max_ms
      );

      SPDLOG_INFO("");
   }

   SPDLOG_INFO("=== Benchmark Complete ===");
}

}  // namespace

TEST(ManyStringEquals, stringInSetVersusOrChain) {
   run();
}
