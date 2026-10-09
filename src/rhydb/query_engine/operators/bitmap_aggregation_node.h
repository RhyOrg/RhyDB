#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/result.h>
#include <nlohmann/json_fwd.hpp>

#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

/// Groups rows by the symbol they carry at a fixed sequence position, e.g. `main.at(123)`.
struct SequencePositionDimension {
   schema::ColumnIdentifier column;
   uint32_t position_idx;  // 0-based
   bool is_nucleotide;
   std::string output_name;  // e.g. "main[1]"

   SequencePositionDimension(
      schema::ColumnIdentifier column,
      uint32_t position_idx,
      bool is_nucleotide,
      std::string output_name
   );

   /// The STRING output column this dimension contributes to the result schema.
   [[nodiscard]] schema::ColumnIdentifier outputColumn() const;

   [[nodiscard]] nlohmann::json toJson() const;
};

/// Groups rows by the value of an indexed string column, straight from its inverted index.
struct IndexedColumnDimension {
   schema::ColumnIdentifier column;
   std::string output_name;

   IndexedColumnDimension(schema::ColumnIdentifier column, std::string output_name);

   [[nodiscard]] schema::ColumnIdentifier outputColumn() const;

   [[nodiscard]] nlohmann::json toJson() const;
};

/// Groups rows by the value of an arbitrary scalar expression a `map()` assignment computes, e.g.
/// `map({week := date.isoWeek()})`
struct ScalarExpressionDimension {
   std::unique_ptr<scalar_expressions::ScalarExpression> expression;
   schema::ColumnType output_type;
   std::string output_name;

   ScalarExpressionDimension(
      std::unique_ptr<scalar_expressions::ScalarExpression> expression,
      schema::ColumnType output_type,
      std::string output_name
   );

   [[nodiscard]] schema::ColumnIdentifier outputColumn() const;

   [[nodiscard]] nlohmann::json toJson() const;
};

/// One grouping dimension of a `BitmapAggregationNode`: a rule for partitioning a filtered row-set
/// into disjoint, value-keyed groups directly from roaring bitmaps, plus the output column it
/// contributes. A variant over the supported kinds lets a single query group on a mix of them;
/// add an alternative to support another kind. Every alternative offers `outputColumn` and
/// `toJson` (so a generic `std::visit` dispatches over them) and a `resolveDimension` overload in
/// the implementation file that resolves it into the `exec_node::ResolvedGroupingDimension` the
/// exec node groups by.
using GroupingDimension =
   std::variant<SequencePositionDimension, IndexedColumnDimension, ScalarExpressionDimension>;

/// Resolved bitmap-aggregation operator. Groups the rows matched by `filter` by a set of
/// `GroupingDimension`s, emitting one row per observed combination of values together with the
/// number of rows carrying it.
///
/// The node itself only evaluates the filter and validates and resolves the dimensions; the
/// aggregation is executed by the Acero node from `exec_node::addBitmapAggregationNode`.
class BitmapAggregationNode final : public QueryNode {
  public:
   std::shared_ptr<storage::Table> table;
   std::unique_ptr<scalar_expressions::ScalarExpression> filter;
   std::vector<GroupingDimension> dimensions;
   std::string count_field_name;

   BitmapAggregationNode(
      std::shared_ptr<storage::Table> table,
      std::unique_ptr<scalar_expressions::ScalarExpression> filter,
      std::vector<GroupingDimension> dimensions,
      std::string count_field_name
   );

   [[nodiscard]] std::vector<schema::ColumnIdentifier> getOutputSchema() const override;

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const override;

   [[nodiscard]] NodeKind kind() const override { return NodeKind::BITMAP_AGGREGATION; }

   [[nodiscard]] nlohmann::json toJson() const override;
};

}  // namespace rhydb::query_engine::operators
