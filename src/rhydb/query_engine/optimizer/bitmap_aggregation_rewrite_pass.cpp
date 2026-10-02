#include "rhydb/query_engine/optimizer/bitmap_aggregation_rewrite_pass.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/bitmap_aggregation_node.h"
#include "rhydb/query_engine/operators/count_filter_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/iso_week.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::optimizer {

namespace {

bool isSequenceColumn(const schema::ColumnIdentifier& column) {
   return column.type == schema::ColumnType::NUCLEOTIDE_SEQUENCE ||
          column.type == schema::ColumnType::AMINO_ACID_SEQUENCE;
}

/// The bare `count()` aggregate (a single count aggregate with no source column) is the only shape
/// this rewrite recognizes.
bool isBareCount(const operators::AggregateNode& node) {
   return node.aggregates.size() == 1 &&
          node.aggregates[0].function == operators::AggregateFunction::COUNT &&
          !node.aggregates[0].source_column.has_value();
}

/// The map assignment producing `field`, or nullopt if there is no map or none produces it (the key
/// is then a passthrough column read straight from the scan).
std::optional<std::reference_wrapper<const operators::MapNode::Assignment>> findAssignment(
   std::optional<std::reference_wrapper<const operators::MapNode>> map,
   const std::string& field
) {
   if (!map.has_value()) {
      return std::nullopt;
   }
   const auto& assignments = map->get().assignments;
   const auto assignment = std::ranges::find_if(assignments, [&](const auto& candidate) {
      return candidate.output_column.name == field;
   });
   if (assignment == assignments.end()) {
      return std::nullopt;
   }
   return *assignment;
}

struct SequencePositionKey {
   const schema::ColumnIdentifier& column;
   uint32_t position;  // 1-indexed character position
};

/// Matches `at(pos, col)` or `at(pos, zstdDecompress(col))` with `col` a direct column reference,
/// returning the column and position. The zstd-decompress is transparent here: the bitmap engine
/// reads the sequence column's per-symbol bitmaps directly, so a decompressed input matches as a
/// raw one does.
std::optional<SequencePositionKey> asSequencePositionKey(
   const operators::MapNode::Assignment& assignment
) {
   const auto* at =
      scalar_expressions::dynCast<scalar_expressions::At>(assignment.expression.get());
   if (at == nullptr) {
      return std::nullopt;
   }
   const auto* decompress =
      scalar_expressions::dynCast<scalar_expressions::ZstdDecompressScalar>(at->input.get());
   const auto* field_ref = scalar_expressions::dynCast<scalar_expressions::FieldRef>(
      decompress != nullptr ? decompress->input.get() : at->input.get()
   );
   if (field_ref == nullptr) {
      return std::nullopt;
   }
   return SequencePositionKey{.column = field_ref->column, .position = at->position};
}

/// The plan beneath a candidate aggregate: the table scan the grouping reads from, and the single
/// map computing the grouping keys if there is one.
struct GroupBySource {
   // Absent = no map, the aggregate reads straight from the scan. Only inspected, hence const.
   std::optional<std::reference_wrapper<const operators::MapNode>> map;
   // Non-const: the rewrite moves the scan's `table` and `filter` out of it.
   operators::TableScanNode& scan;
};

std::optional<GroupBySource> groupBySource(operators::QueryNode& aggregate_child) {
   const auto* map = dynamic_cast<const operators::MapNode*>(&aggregate_child);

   // The table scan sits below the map when there is one, otherwise directly below the aggregate.
   operators::QueryNode* scan_node = &aggregate_child;
   if (map != nullptr) {
      scan_node = map->child.get();
   }

   auto* scan = dynamic_cast<operators::TableScanNode*>(scan_node);
   if (scan == nullptr) {
      return std::nullopt;
   }
   std::optional<std::reference_wrapper<const operators::MapNode>> map_ref;
   if (map != nullptr) {
      map_ref = *map;
   }
   return GroupBySource{.map = map_ref, .scan = *scan};
}

/// The dimension for `group_by_field` when the map produces it as a sequence-position lookup
/// (`field := seq.at(pos)`), or nullopt otherwise.
std::optional<operators::GroupingDimension> matchSequencePositionDimension(
   const GroupBySource& source,
   const schema::ColumnIdentifier& group_by_field
) {
   const auto assignment = findAssignment(source.map, group_by_field.name);
   if (!assignment.has_value()) {
      return std::nullopt;
   }
   const auto key = asSequencePositionKey(assignment->get());
   if (!key.has_value()) {
      return std::nullopt;
   }
   const auto column = source.scan.table->schema->getColumn(key->column.name);
   if (!column.has_value() || !isSequenceColumn(column.value())) {
      return std::nullopt;
   }
   return operators::SequencePositionDimension{
      column.value(),
      key->position - 1,
      column->type == schema::ColumnType::NUCLEOTIDE_SEQUENCE,
      group_by_field.name
   };
}

/// The dimension for `group_by_field` when it resolves to an indexed (dictionary-encoded) column
/// whose inverted index can be reused directly. Either the group-by field is such a scan column,
/// or the map produces it as a bare field reference (`x := some_dict_column`). Returns nullopt
/// otherwise; a computed expression or a non-indexed column is left to the generic matcher.
std::optional<operators::GroupingDimension> matchIndexedColumnDimension(
   const GroupBySource& source,
   const schema::ColumnIdentifier& group_by_field
) {
   // Resolve the underlying scan column: the group-by field itself when it is a plain scan column,
   // or the column a bare `x := column` map assignment references. A map assignment that is
   // anything other than a bare field reference (at(), a function, ...) is left to the other
   // matchers.
   std::string column_name = group_by_field.name;
   if (const auto assignment = findAssignment(source.map, group_by_field.name)) {
      const auto* field_ref = scalar_expressions::dynCast<scalar_expressions::FieldRef>(
         assignment->get().expression.get()
      );
      if (field_ref == nullptr) {
         return std::nullopt;
      }
      column_name = field_ref->column.name;
   }
   const auto column = source.scan.table->schema->getColumn(column_name);
   if (!column.has_value() || column->type != schema::ColumnType::DICTIONARY_ENCODED) {
      return std::nullopt;
   }
   return operators::IndexedColumnDimension{column.value(), group_by_field.name};
}

/// A scalar output type the bitmap aggregation can group on (and emit): every value type its output
/// materialization can build. Sequence / compressed columns are excluded -- they are not a single
/// groupable value.
bool isGroupableScalarType(schema::ColumnType type) {
   switch (type) {
      case schema::ColumnType::STRING:
      case schema::ColumnType::DICTIONARY_ENCODED:
      case schema::ColumnType::INT32:
      case schema::ColumnType::INT64:
      case schema::ColumnType::FLOAT:
      case schema::ColumnType::BOOL:
      case schema::ColumnType::DATE32:
         return true;
      case schema::ColumnType::AMINO_ACID_SEQUENCE:
      case schema::ColumnType::NUCLEOTIDE_SEQUENCE:
      case schema::ColumnType::ZSTD_COMPRESSED_STRING:
         return false;
   }
   return false;
}

/// Whether the grouper can evaluate `expression` per row through Arrow: it must be composed only of
/// nodes the Arrow translation handles that yield a groupable scalar -- field references to
/// (non-sequence) scalar columns, literals, `at` and `isoWeek`. This deliberately excludes anything
/// sequence- or decompression-specific (those are grouped by the dedicated sequence-position path),
/// so the rewrite only claims expressions the node can actually execute.
bool isEvaluableGroupableExpression(
   const scalar_expressions::ScalarExpression& expression,
   const schema::TableSchema& table_schema
) {
   using namespace scalar_expressions;
   switch (expression.kind()) {
      case ScalarExpression::Kind::FIELD_REF: {
         const auto* field_ref = dynCast<FieldRef>(&expression);
         const auto column = table_schema.getColumn(field_ref->column.name);
         return column.has_value() && isGroupableScalarType(column->type);
      }
      case ScalarExpression::Kind::INT32_LITERAL:
      case ScalarExpression::Kind::INT64_LITERAL:
      case ScalarExpression::Kind::FLOAT_LITERAL:
      case ScalarExpression::Kind::STRING_LITERAL:
      case ScalarExpression::Kind::BOOL_LITERAL:
      case ScalarExpression::Kind::DATE_LITERAL:
         return true;
      case ScalarExpression::Kind::AT:
         return isEvaluableGroupableExpression(*dynCast<At>(&expression)->input, table_schema);
      case ScalarExpression::Kind::ISO_WEEK:
         return isEvaluableGroupableExpression(*dynCast<IsoWeek>(&expression)->input, table_schema);
      default:
         return false;
   }
}

/// The dimension for `group_by_field` when the map computes it with a general scalar expression the
/// grouper can evaluate (e.g. `week := date.isoWeek()`), or nullopt otherwise. This is the
/// catch-all after the cheaper dedicated matchers: the grouper evaluates the expression over the
/// rows and buckets by the (typed) result. Declines expressions that read no column -- there is
/// nothing to materialize per row -- so a pure literal falls back to the generic path.
std::optional<operators::GroupingDimension> matchScalarExpressionDimension(
   const GroupBySource& source,
   const schema::ColumnIdentifier& group_by_field
) {
   const auto assignment = findAssignment(source.map, group_by_field.name);
   if (!assignment.has_value()) {
      return std::nullopt;  // a plain scan column, not a map-computed expression
   }
   const auto& expression = *assignment->get().expression;
   if (expression.freeIUs().empty() || !isGroupableScalarType(expression.type()) ||
       !isEvaluableGroupableExpression(expression, *source.scan.table->schema)) {
      return std::nullopt;
   }
   return operators::ScalarExpressionDimension{
      expression.clone(), expression.type(), group_by_field.name
   };
}

/// Resolves one grouping key to its bitmap dimension, or nullopt if no supported dimension matches
/// (which makes the whole rewrite decline). Add a matcher and one `if` to support another shape.
std::optional<operators::GroupingDimension> resolveDimension(
   const GroupBySource& source,
   const schema::ColumnIdentifier& group_by_field
) {
   if (auto dimension = matchSequencePositionDimension(source, group_by_field)) {
      return dimension;
   }
   if (auto dimension = matchIndexedColumnDimension(source, group_by_field)) {
      return dimension;
   }
   if (auto dimension = matchScalarExpressionDimension(source, group_by_field)) {
      return dimension;
   }
   return std::nullopt;
}

bool isBitmapBacked(const operators::GroupingDimension& dimension) {
   return !std::holds_alternative<operators::ScalarExpressionDimension>(dimension);
}

}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
operators::QueryNodePtr BitmapAggregationRewritePass::operator()(operators::AggregateNode& node) {
   propagateToNode(node.child);

   if (!isBareCount(node)) {
      return nullptr;
   }

   // Whenever the plan is not a shape this rewrite can resolve, decline (nullptr) and leave the
   // generic map/groupBy pipeline to handle it rather than erroring.
   auto source = groupBySource(*node.child);
   if (!source.has_value()) {
      return nullptr;
   }

   // A full `count(*)` with no grouping keys is the filter's cardinality: read it straight off the
   // scan's filter bitmap. The map (if any) only decompresses columns nobody reads here and does
   // not change the row count, so it is dropped. This is the fast path for `default.count()` /
   // `default.groupBy({n := count()})`.
   if (node.group_by_fields.empty()) {
      return std::make_unique<operators::CountFilterNode>(
         std::move(source->scan.table),
         std::move(source->scan.filter),
         node.aggregates[0].output_name
      );
   }

   std::vector<operators::GroupingDimension> dimensions;
   dimensions.reserve(node.group_by_fields.size());
   for (const auto& group_by_field : node.group_by_fields) {
      auto dimension = resolveDimension(*source, group_by_field);
      if (!dimension.has_value()) {
         return nullptr;
      }
      dimensions.push_back(std::move(dimension.value()));
   }

   // don't apply optimization if no group-by dimension is bitmap-backed
   if (std::ranges::none_of(dimensions, isBitmapBacked)) {
      return nullptr;
   }

   return std::make_unique<operators::BitmapAggregationNode>(
      std::move(source->scan.table),
      std::move(source->scan.filter),
      std::move(dimensions),
      node.aggregates[0].output_name
   );
}

}  // namespace rhydb::query_engine::optimizer
