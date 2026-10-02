#pragma once

#include <map>
#include <string_view>
#include <vector>

#include <arrow/result.h>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

/// Source-type node that reports the output schema of its child as data rows,
/// one row per child field. The child's query plan is never executed; only its
/// output schema is inspected.
///
/// This is a "pipeline breaker": it produces a fresh result relation rather than
/// forwarding its child's rows. Schema-preserving operators that do not need to
/// push work into an underlying data source (e.g. `order`, `limit`, `map`,
/// `project`) can be chained after it. `filter()` currently cannot, because it is
/// only realizable when pushed into a table scan, and there is none above
/// `schema()`.
class SchemaNode final : public QueryNode {
  public:
   static constexpr std::string_view FIELD_NAME_COLUMN = "fieldName";
   static constexpr std::string_view TYPE_COLUMN = "type";

   QueryNodePtr child;

   explicit SchemaNode(QueryNodePtr child);

   [[nodiscard]] std::vector<schema::ColumnIdentifier> getOutputSchema() const override;

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const override;

   [[nodiscard]] NodeKind kind() const override { return NodeKind::SCHEMA; }

   [[nodiscard]] nlohmann::json toJson() const override;
};

}  // namespace rhydb::query_engine::operators
