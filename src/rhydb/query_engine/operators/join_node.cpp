#include "rhydb/query_engine/operators/join_node.h"

#include <string_view>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/type.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/panic.h"

namespace rhydb::query_engine::operators {

using arrow::acero::JoinType;

JoinNode::JoinNode(
   QueryNodePtr left,
   QueryNodePtr right,
   std::vector<schema::ColumnIdentifier> left_keys,
   std::vector<schema::ColumnIdentifier> right_keys,
   JoinType join_type
)
    : left(std::move(left)),
      right(std::move(right)),
      left_keys(std::move(left_keys)),
      right_keys(std::move(right_keys)),
      join_type(join_type) {
   // Equi-join key vectors are paired positionally (left_keys[i] == right_keys[i]), so they
   // must have the same length
   RHYDB_ASSERT_EQ(this->left_keys.size(), this->right_keys.size());
}

std::string_view joinTypeToString(JoinType join_type) {
   switch (join_type) {
      case JoinType::INNER:
         return "inner";
      case JoinType::LEFT_OUTER:
         return "left";
      case JoinType::RIGHT_OUTER:
         return "right";
      case JoinType::FULL_OUTER:
         return "full";
      case JoinType::LEFT_SEMI:
         return "leftSemi";
      case JoinType::RIGHT_SEMI:
         return "rightSemi";
      case JoinType::LEFT_ANTI:
         return "leftAnti";
      case JoinType::RIGHT_ANTI:
         return "rightAnti";
   }
   RHYDB_UNREACHABLE();
}

std::vector<schema::ColumnIdentifier> JoinNode::getOutputSchema() const {
   // Semi/anti joins act as a filter on one input and emit only that side's columns.
   if (join_type == JoinType::LEFT_SEMI || join_type == JoinType::LEFT_ANTI) {
      return left->getOutputSchema();
   }
   if (join_type == JoinType::RIGHT_SEMI || join_type == JoinType::RIGHT_ANTI) {
      return right->getOutputSchema();
   }
   auto output = left->getOutputSchema();
   auto right_schema = right->getOutputSchema();
   output.insert(output.end(), right_schema.begin(), right_schema.end());
   return output;
}

arrow::Result<arrow::acero::ExecNode*> JoinNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options
) const {
   ARROW_ASSIGN_OR_RAISE(auto* left_node, left->addToExecPlan(plan, tables, query_options));
   ARROW_ASSIGN_OR_RAISE(auto* right_node, right->addToExecPlan(plan, tables, query_options));

   std::vector<arrow::FieldRef> left_key_refs;
   left_key_refs.reserve(left_keys.size());
   for (const auto& key : left_keys) {
      left_key_refs.emplace_back(key.name);
   }
   std::vector<arrow::FieldRef> right_key_refs;
   right_key_refs.reserve(right_keys.size());
   for (const auto& key : right_keys) {
      right_key_refs.emplace_back(key.name);
   }

   const arrow::acero::HashJoinNodeOptions options{
      join_type, std::move(left_key_refs), std::move(right_key_refs),
   };
   return arrow::acero::MakeExecNode("hashjoin", &plan, {left_node, right_node}, options);
}

nlohmann::json JoinNode::toJson() const {
   nlohmann::json keys = nlohmann::json::array();
   for (size_t i = 0; i < left_keys.size(); ++i) {
      keys.push_back({{"left", left_keys[i].name}, {"right", right_keys[i].name}});
   }
   return {
      {"type", nodeKindToString(kind())},
      {"joinType", joinTypeToString(join_type)},
      {"on", std::move(keys)},
      {"left", left->toJson()},
      {"right", right->toJson()},
   };
}

}  // namespace rhydb::query_engine::operators
