#pragma once

#include <string_view>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/query_plan.h"

namespace rhydb::query_engine {

class Planner {
  public:
   /// Runs all optimization passes on the query tree and returns the optimized root. Exposed
   /// separately from planQuery so tests can assert on the optimized tree without executing it.
   static operators::QueryNodePtr optimize(
      operators::QueryNodePtr node,
      std::string_view request_id
   );

   static QueryPlan planQuery(
      operators::QueryNodePtr node,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options,
      std::string_view request_id
   );

   static QueryPlan planSaneqlQuery(
      std::string_view query_string,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options,
      std::string_view request_id
   );
};

}  // namespace rhydb::query_engine
