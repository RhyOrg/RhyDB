#pragma once

#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include <arrow/acero/type_fwd.h>
#include <arrow/result.h>
#include <nlohmann/json_fwd.hpp>

#include "rhydb/schema/database_schema.h"

namespace rhydb::config {
class QueryOptions;
}  // namespace rhydb::config

namespace rhydb::storage {
class Table;
}  // namespace rhydb::storage

namespace rhydb::query_engine::operators {

enum class NodeKind : uint8_t {
   AGGREGATE,
   PROJECT,
   MAP,
   ORDER_BY,
   ORDER_BY_WITH_LIMIT,
   FETCH,
   FILTER,
   UNRESOLVED_MUTATIONS_NUCLEOTIDE,
   UNRESOLVED_MUTATIONS_AMINO_ACID,
   UNRESOLVED_INSERTIONS_NUCLEOTIDE,
   UNRESOLVED_INSERTIONS_AMINO_ACID,
   UNRESOLVED_MOST_RECENT_COMMON_ANCESTOR,
   UNRESOLVED_PHYLO_SUBTREE,
   MUTATIONS_NUCLEOTIDE,
   MUTATIONS_AMINO_ACID,
   INSERTIONS_NUCLEOTIDE,
   INSERTIONS_AMINO_ACID,
   MOST_RECENT_COMMON_ANCESTOR,
   PHYLO_SUBTREE,
   TABLE_SCAN,
   COUNT_FILTER,
   UNION_ALL,
   JOIN,
   SCHEMA,
   TABLES_LIST,
   BITMAP_AGGREGATION,
   TRANSITIVE_CLOSURE,
};

class QueryNode {
  public:
   virtual ~QueryNode() = default;

   /// Translate this node (including its sub-nodes) to `arrow::acero` nodes and add them to an
   /// existing `ExecPlan`. Returns the top `ExecNode*` within that plan.
   [[nodiscard]] virtual arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const = 0;

   [[nodiscard]] virtual std::vector<schema::ColumnIdentifier> getOutputSchema() const = 0;

   [[nodiscard]] virtual NodeKind kind() const = 0;

   /// Serializes this node (and its children) into a JSON representation that can be
   /// displayed elsewhere for debugging or query-plan inspection.
   [[nodiscard]] virtual nlohmann::json toJson() const = 0;
};

using QueryNodePtr = std::unique_ptr<QueryNode>;

[[nodiscard]] std::string_view nodeKindToString(NodeKind kind);

[[nodiscard]] nlohmann::json columnToJson(const schema::ColumnIdentifier& column);

[[nodiscard]] nlohmann::json columnsToJson(const std::vector<schema::ColumnIdentifier>& columns);

}  // namespace rhydb::query_engine::operators
