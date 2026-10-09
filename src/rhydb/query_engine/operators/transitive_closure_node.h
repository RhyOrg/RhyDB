#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <arrow/result.h>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

/// The `child` is a subquery that produces the columns `to_column` and `from_column`.
/// It is materialized and interpreted as a set of directed edges: every input row contributes an
/// edge from the value in its `from_column` to the value in its `to_column`. Both must be
/// string-typed columns of the child's output. Rows with a null value in either endpoint are
/// ignored (a lineage relation table, for example, stores a null parent for each root). The node
/// emits one output row `{from, to}` for every ordered pair of vertices `(a, b)` such that `b` is
/// reachable from `a` by following one or more edges.
///
/// When `include_vertices` is true, the reflexive pair `(v, v)` is additionally emitted for
/// every vertex `v` appearing in the relation, yielding the reflexive-transitive closure. This
/// is what lets a lineage be counted together with all of its sublineages: joining the
/// closure's `to` column against a data table's lineage column and grouping by `from` sums, for
/// each lineage, all of its descendants and — thanks to the reflexive pair — itself.
///
/// `starting_from`, when set, restricts the sources: only pairs whose `from` is one of the given
/// vertices are emitted, and only the edges reachable from those vertices are ever walked. This
/// turns the O(V * (V + E)) all-sources closure into an O(k * (V + E)) search over the k requested
/// sources, which is what a query asking for the descendants of one node needs. Requested vertices
/// that do not occur in the relation contribute no rows, not even a reflexive pair. The vertices
/// are given as constant string-valued scalar expressions (e.g. string literals), which are
/// evaluated once when the node is added to the plan; null values name no vertex.
class TransitiveClosureNode final : public QueryNode {
  public:
   static constexpr std::string_view FROM_COLUMN = "from";
   static constexpr std::string_view TO_COLUMN = "to";

   QueryNodePtr child;
   std::string from_column;
   std::string to_column;
   bool include_vertices;
   std::optional<std::vector<std::unique_ptr<scalar_expressions::ScalarExpression>>> starting_from;

   TransitiveClosureNode(
      QueryNodePtr child,
      std::string from_column,
      std::string to_column,
      bool include_vertices,
      std::optional<std::vector<std::unique_ptr<scalar_expressions::ScalarExpression>>>
         starting_from = std::nullopt
   );

   [[nodiscard]] std::vector<schema::ColumnIdentifier> getOutputSchema() const override;

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const override;

   [[nodiscard]] NodeKind kind() const override { return NodeKind::TRANSITIVE_CLOSURE; }

   [[nodiscard]] nlohmann::json toJson() const override;
};

}  // namespace rhydb::query_engine::operators
