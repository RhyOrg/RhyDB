#include "rhydb/query_engine/scalar_expressions/is_null.h"

#include <utility>

#include <arrow/compute/api.h>
#include <fmt/format.h>

#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/column/column_type_visitor.h"

namespace rhydb::query_engine::scalar_expressions {

IsNull::IsNull(schema::ColumnIdentifier column)
    : column(std::move(column)) {}

std::string IsNull::toString() const {
   return fmt::format("{} IS NULL", column.name);
}

std::vector<schema::ColumnIdentifier> IsNull::freeIUs() const {
   return {column};
}

arrow::Result<arrow::compute::Expression> IsNull::toArrowExpression() const {
   return arrow::compute::is_null(arrow::compute::field_ref(column.name));
}

std::unique_ptr<ScalarExpression> IsNull::rewrite(
   const storage::Table& /*table*/,
   ScalarExpression::AmbiguityMode /*mode*/
) const {
   return std::make_unique<IsNull>(column);
}

std::unique_ptr<filter::operators::Operator> IsNull::compile(const storage::Table& table) const {
   const auto& maybe_target_column = table.schema->getColumn(column.name);
   CHECK_RHYDB_QUERY(
      maybe_target_column.has_value(),
      "The column '{}' is not contained in the database",
      column.name
   );
   auto target_column = maybe_target_column.value();

   return rhydb::storage::column::visit(target_column.type, [&]<storage::column::Column Column>() {
      const auto& value_column = table.getColumn<Column>(column.name);
      return std::make_unique<filter::operators::IndexScan>(
         Bitmap{&value_column.null_bitmap}, table.row_layout
      );
   });
}

}  // namespace rhydb::query_engine::scalar_expressions
