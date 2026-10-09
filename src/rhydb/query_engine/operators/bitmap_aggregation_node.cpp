#include "rhydb/query_engine/operators/bitmap_aggregation_node.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/compute/expression.h>
#include <arrow/result.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/bitmap_aggregation.h"
#include "rhydb/query_engine/exec_node/dimension_index.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/compute_filter.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

namespace {

template <typename SymbolType>
void checkPositionInBounds(
   const storage::Table& table,
   const SequencePositionDimension& dimension
) {
   const auto& column = table.getColumn<typename SymbolType::Column>(dimension.column.name);
   CHECK_RHYDB_QUERY(
      dimension.position_idx < column.metadata->reference_sequence.size(),
      "{}.at({}) is out of bounds: the {} sequence has length {}",
      column.metadata->column_name,
      dimension.position_idx + 1,
      SymbolType::SYMBOL_NAME_LOWER_CASE,
      column.metadata->reference_sequence.size()
   );
}

exec_node::ResolvedGroupingDimension resolveDimension(
   const SequencePositionDimension& dimension,
   const storage::Table& table
) {
   if (dimension.is_nucleotide) {
      checkPositionInBounds<Nucleotide>(table, dimension);
   } else {
      checkPositionInBounds<AminoAcid>(table, dimension);
   }
   return exec_node::ResolvedSequencePositionDimension{
      .column_name = dimension.column.name,
      .position_idx = dimension.position_idx,
      .is_nucleotide = dimension.is_nucleotide,
   };
}

exec_node::ResolvedGroupingDimension resolveDimension(
   const IndexedColumnDimension& dimension,
   const storage::Table& /*table*/
) {
   return exec_node::ResolvedIndexedColumnDimension{.column_name = dimension.column.name};
}

/// The distinct columns `expression` reads, resolved against the table so each carries its real
/// column type (needed to materialize it and bind the expression).
std::vector<schema::ColumnIdentifier> resolveReferencedColumns(
   const scalar_expressions::ScalarExpression& expression,
   const storage::Table& table
) {
   std::vector<schema::ColumnIdentifier> referenced;
   std::unordered_set<std::string> seen;
   for (const auto& column : expression.freeIUs()) {
      if (seen.insert(column.name).second) {
         const auto resolved = table.schema->getColumn(column.name);
         CHECK_RHYDB_QUERY(
            resolved.has_value(), "bitmap aggregation references unknown column '{}'", column.name
         );
         referenced.push_back(resolved.value());
      }
   }
   return referenced;
}

exec_node::ResolvedGroupingDimension resolveDimension(
   const ScalarExpressionDimension& dimension,
   const storage::Table& table
) {
   auto referenced = resolveReferencedColumns(*dimension.expression, table);
   const auto arrow_expression = dimension.expression->toArrowExpression();
   CHECK_RHYDB_QUERY(arrow_expression.ok(), "{}", arrow_expression.status().ToString());
   const auto input_schema = exec_node::columnsToArrowSchema(referenced);
   auto bound_expression = arrow_expression->Bind(*input_schema);
   CHECK_RHYDB_QUERY(bound_expression.ok(), "{}", bound_expression.status().ToString());
   return exec_node::ResolvedScalarExpressionDimension{
      .referenced_columns = std::move(referenced),
      .bound_expression = std::move(bound_expression).ValueUnsafe(),
      .output_type = dimension.output_type,
   };
}

}  // namespace

SequencePositionDimension::SequencePositionDimension(
   schema::ColumnIdentifier column,
   uint32_t position_idx,
   bool is_nucleotide,
   std::string output_name
)
    : column(std::move(column)),
      position_idx(position_idx),
      is_nucleotide(is_nucleotide),
      output_name(std::move(output_name)) {}

schema::ColumnIdentifier SequencePositionDimension::outputColumn() const {
   return {.name = output_name, .type = schema::ColumnType::STRING};
}

nlohmann::json SequencePositionDimension::toJson() const {
   return {
      {"kind", "sequencePosition"},
      {"column", columnToJson(column)},
      {"position", position_idx},
      {"isNucleotide", is_nucleotide},
      {"outputName", output_name},
   };
}

IndexedColumnDimension::IndexedColumnDimension(
   schema::ColumnIdentifier column,
   std::string output_name
)
    : column(std::move(column)),
      output_name(std::move(output_name)) {}

schema::ColumnIdentifier IndexedColumnDimension::outputColumn() const {
   return {.name = output_name, .type = schema::ColumnType::STRING};
}

nlohmann::json IndexedColumnDimension::toJson() const {
   return {
      {"kind", "indexedColumn"},
      {"column", columnToJson(column)},
      {"outputName", output_name},
   };
}

ScalarExpressionDimension::ScalarExpressionDimension(
   std::unique_ptr<scalar_expressions::ScalarExpression> expression,
   schema::ColumnType output_type,
   std::string output_name
)
    : expression(std::move(expression)),
      output_type(output_type),
      output_name(std::move(output_name)) {}

schema::ColumnIdentifier ScalarExpressionDimension::outputColumn() const {
   return {.name = output_name, .type = output_type};
}

nlohmann::json ScalarExpressionDimension::toJson() const {
   return {
      {"kind", "scalarExpression"},
      {"expression", expression->toString()},
      {"outputType", std::string{schema::columnTypeToString(output_type)}},
      {"outputName", output_name},
   };
}

BitmapAggregationNode::BitmapAggregationNode(
   std::shared_ptr<storage::Table> table,
   std::unique_ptr<scalar_expressions::ScalarExpression> filter,
   std::vector<GroupingDimension> dimensions,
   std::string count_field_name
)
    : table(std::move(table)),
      filter(std::move(filter)),
      dimensions(std::move(dimensions)),
      count_field_name(std::move(count_field_name)) {}

std::vector<schema::ColumnIdentifier> BitmapAggregationNode::getOutputSchema() const {
   std::vector<schema::ColumnIdentifier> output_fields;
   output_fields.reserve(dimensions.size() + 1);
   for (const auto& dimension : dimensions) {
      output_fields.emplace_back(
         std::visit([](const auto& dim) { return dim.outputColumn(); }, dimension)
      );
   }
   output_fields.emplace_back(count_field_name, schema::ColumnType::INT64);
   return output_fields;
}

nlohmann::json BitmapAggregationNode::toJson() const {
   nlohmann::json dimensions_json = nlohmann::json::array();
   for (const auto& dimension : dimensions) {
      dimensions_json.push_back(std::visit([](const auto& dim) { return dim.toJson(); }, dimension)
      );
   }
   return {
      {"type", nodeKindToString(kind())},
      {"filter", filter->toString()},
      {"dimensions", std::move(dimensions_json)},
      {"countFieldName", count_field_name},
   };
}

arrow::Result<arrow::acero::ExecNode*> BitmapAggregationNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& query_options
) const {
   auto filter_bitmap = compileFilter(filter, table)->evaluate();

   // Resolve each dimension against the table into the form the exec node groups by. This also
   // validates it (e.g. a sequence position out of range throws here), so invalid queries are
   // rejected while planning rather than mid-execution.
   std::vector<exec_node::ResolvedGroupingDimension> resolved_dimensions;
   resolved_dimensions.reserve(dimensions.size());
   for (const auto& dimension : dimensions) {
      resolved_dimensions.push_back(
         std::visit([&](const auto& dim) { return resolveDimension(dim, *table); }, dimension)
      );
   }

   // `materialization_cutoff` is the batch-size-minus-one knob the rest of the pipeline (e.g. the
   // table scan) uses, so this output is sized the same way.
   return exec_node::addBitmapAggregationNode(
      plan,
      table,
      std::move(filter_bitmap),
      std::move(resolved_dimensions),
      exec_node::columnsToArrowSchema(getOutputSchema()),
      query_options.materialization_cutoff + 1
   );
}

}  // namespace rhydb::query_engine::operators
