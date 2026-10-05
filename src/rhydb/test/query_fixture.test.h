#pragma once

#include <filesystem>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/append/table_inserter.h"
#include "rhydb/common/lineage_tree.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/database_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/storage/reference_genomes.h"

namespace rhydb::test {

/**
 * Creates a test suite for a query test.
 * The test suite executes multiple queries on the same dataset.
 *
 * @param TEST_SUITE_NAME The name of the test suite (must be unique across all generated suites).
 * @param TEST_DATA The dataset to be used for the test suite.
 * Must be an instance of rhydb::test::QueryTestData.
 *
 * @param TEST_VALUES The queries to be executed on the given dataset and the expected results.
 * Must be of the form `::testing::Values(rhydb::test::QueryTestScenario... scenarios)`.
 */
#define QUERY_TEST(TEST_SUITE_NAME, TEST_DATA, TEST_VALUES)                                       \
   namespace {                                                                                    \
   struct TEST_SUITE_NAME##DataContainer {                                                        \
      const rhydb::test::QueryTestData test_data = TEST_DATA;                                     \
   };                                                                                             \
   }                                                                                              \
                                                                                                  \
   using TEST_SUITE_NAME##FixtureAlias =                                                          \
      rhydb::test::QueryTestFixture<TEST_SUITE_NAME##DataContainer>;                              \
                                                                                                  \
   template <>                                                                                    \
   std::shared_ptr<rhydb::Database> TEST_SUITE_NAME##FixtureAlias::shared_database = nullptr;     \
                                                                                                  \
   INSTANTIATE_TEST_SUITE_P(                                                                      \
      TEST_SUITE_NAME, TEST_SUITE_NAME##FixtureAlias, TEST_VALUES, rhydb::test::printScenarioName \
   );                                                                                             \
                                                                                                  \
   TEST_P(TEST_SUITE_NAME##FixtureAlias, testQuery) {                                             \
      const auto& scenario = GetParam();                                                          \
      runTest(scenario);                                                                          \
   };

struct QueryTestData {
   const std::vector<nlohmann::json> ndjson_input_data;
   const std::string database_config;
   const ReferenceGenomes reference_genomes;
   const std::map<std::filesystem::path, common::LineageTreeAndIdMap> lineage_trees;
   const common::PhyloTree phylo_tree_file;
   const bool without_unaligned_sequences = false;
};

struct QueryTestScenario {
   std::string name;
   std::string query;
   nlohmann::json expected_query_result;
   std::string expected_error_message;
   std::optional<config::QueryOptions> query_options;
};

std::string printScenarioName(const ::testing::TestParamInfo<QueryTestScenario>& scenario);

nlohmann::json executeQueryToJsonArray(
   query_engine::QueryPlan& query_plan,
   uint64_t timeout_in_seconds = 3
);

template <typename DataContainer>
class QueryTestFixture : public ::testing::TestWithParam<QueryTestScenario> {
  public:
   static std::shared_ptr<Database> shared_database;

   static void SetUpTestSuite() {
      const DataContainer data_container;
      const QueryTestData& test_data = data_container.test_data;

      auto database = std::make_shared<Database>();
      initialize::Initializer::createTableInDatabase(
         schema::TableName::getDefault(),
         config::DatabaseConfig::getValidatedConfig(test_data.database_config),
         test_data.reference_genomes,
         test_data.lineage_trees,
         test_data.phylo_tree_file,
         test_data.without_unaligned_sequences,
         *database
      );

      std::stringstream ndjson_objects;
      for (const auto& object : test_data.ndjson_input_data) {
         ndjson_objects << object.dump() << "\n";
      }
      std::istream ndjson_objects_istream(ndjson_objects.rdbuf());

      database->appendData(schema::TableName::getDefault(), ndjson_objects_istream);

      shared_database = std::move(database);
   }

   void runTest(const rhydb::test::QueryTestScenario& scenario) {
      if (!shared_database) {
         FAIL() << "There was an error when setting up the test suite. Database not initialized.";
      }
      const auto query_options =
         scenario.query_options.value_or(config::RuntimeConfig::withDefaults().query_options);
      if (!scenario.expected_error_message.empty()) {
         try {
            auto query_plan = query_engine::Planner::planSaneqlQuery(
               scenario.query, shared_database->tables, query_options, "some_id"
            );
            executeQueryToJsonArray(query_plan);
            FAIL() << "Expected an error in test case, but nothing was thrown";
         } catch (const std::exception& e) {
            EXPECT_EQ(std::string(e.what()), scenario.expected_error_message);
         }
      } else {
         auto query_plan = query_engine::Planner::planSaneqlQuery(
            scenario.query, shared_database->tables, query_options, "some_id"
         );
         auto actual_ndjson_result_as_array = executeQueryToJsonArray(query_plan);
         ASSERT_EQ(actual_ndjson_result_as_array, scenario.expected_query_result);
      }
   }
};

}  // namespace rhydb::test
