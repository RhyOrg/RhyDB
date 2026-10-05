#include "rhydb/query_engine/scalar_expressions/lineage_filter.h"

#include <cctype>
#include <optional>
#include <ranges>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"

namespace rhydb::query_engine::scalar_expressions {

using rhydb::common::RecombinantEdgeFollowingMode;
using rhydb::storage::column::DictionaryEncodedColumn;

LineageFilter::LineageFilter(
   schema::ColumnIdentifier column,
   std::optional<std::string> lineage,
   std::optional<RecombinantEdgeFollowingMode> sublineage_mode
)
    : column(std::move(column)),
      lineage(std::move(lineage)),
      sublineage_mode(sublineage_mode) {}

std::string LineageFilter::toString() const {
   if (!lineage.has_value()) {
      return "NULL";
   }
   if (sublineage_mode.has_value()) {
      return "'" + lineage.value() + "*'";
   }
   return "'" + lineage.value() + "'";
}

std::vector<schema::ColumnIdentifier> LineageFilter::freeIUs() const {
   return {column};
}

std::optional<const roaring::Roaring*> LineageFilter::getBitmapForValue(
   const DictionaryEncodedColumn& lineage_column
) const {
   if (lineage == std::nullopt) {
      return lineage_column.filter(std::nullopt);
   }

   const auto value_id_opt = lineage_column.getValueId(lineage.value());

   CHECK_RHYDB_QUERY(
      value_id_opt.has_value(),
      "The lineage '{}' is not a valid lineage for column '{}'.",
      lineage.value(),
      column.name
   );

   const Idx value_id = value_id_opt.value();

   if (sublineage_mode.has_value()) {
      return lineage_column.getLineageIndex()->filterIncludingSublineages(
         value_id, sublineage_mode.value()
      );
   }
   return lineage_column.getLineageIndex()->filterExcludingSublineages(value_id);
}

std::unique_ptr<ScalarExpression> LineageFilter::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<LineageFilter>(column, lineage, sublineage_mode);
}

std::unique_ptr<filter::operators::Operator> LineageFilter::compile(const storage::Table& table
) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.hasColumn<DictionaryEncodedColumn>(column.name) &&
         table.getColumn<DictionaryEncodedColumn>(column.name).getLineageIndex().has_value(),
      "The database does not contain a lineage index for the column '{}'",
      column.name
   );

   const auto& lineage_column = table.getColumn<DictionaryEncodedColumn>(column.name);
   std::optional<const roaring::Roaring*> bitmap = getBitmapForValue(lineage_column);

   if (bitmap == std::nullopt) {
      return std::make_unique<filter::operators::Empty>(table.row_layout);
   }
   return std::make_unique<filter::operators::IndexScan>(Bitmap{bitmap.value()}, table.row_layout);
}

}  // namespace rhydb::query_engine::scalar_expressions
