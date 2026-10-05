#include "rhydb/test/query_fixture.test.h"

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "rhydb/query_engine/command/write_command.h"
#include "rhydb/query_engine/exec_node/ndjson_sink.h"

namespace rhydb::test {

std::string printScenarioName(const ::testing::TestParamInfo<QueryTestScenario>& scenario) {
   return scenario.param.name;
}

nlohmann::json executeQueryToJsonArray(
   query_engine::QueryPlan& query_plan,
   uint64_t timeout_in_seconds
) {
   std::stringstream buffer;
   query_engine::exec_node::NdjsonSink output_sink{&buffer, query_plan.results_schema};
   query_plan.executeAndWrite(output_sink, timeout_in_seconds);
   nlohmann::json result = nlohmann::json::array();
   std::string line;
   while (std::getline(buffer, line)) {
      result.push_back(nlohmann::json::parse(line));
   }
   return result;
}

nlohmann::json writeResultToJson(const std::shared_ptr<arrow::Table>& write_result) {
   std::stringstream buffer;
   query_engine::exec_node::NdjsonSink output_sink{&buffer, write_result->schema()};
   const auto status = query_engine::command::writeToSink(*write_result, output_sink);
   if (!status.ok()) {
      throw std::runtime_error(status.ToString());
   }
   return nlohmann::json::parse(buffer.str());
}

}  // namespace rhydb::test
