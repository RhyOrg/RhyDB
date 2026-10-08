#include "rhydb/query_engine/scalar_expressions/phylo_child_filter.h"

#include <optional>
#include <utility>

#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/bitmap_producer.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

PhyloChildFilter::PhyloChildFilter(schema::ColumnIdentifier column, std::string internal_node)
    : column(std::move(column)),
      internal_node(std::move(internal_node)) {}

std::string PhyloChildFilter::toString() const {
   return fmt::format("column {} phylo_child_of {}", column.name, internal_node);
};

std::vector<schema::ColumnIdentifier> PhyloChildFilter::freeIUs() const {
   return {column};
}

namespace {
std::unique_ptr<filter::operators::Operator> createMatchingBitmap(
   const storage::column::StringColumn& string_column,
   const std::string& internal_node,
   storage::column::RowLayout row_layout
) {
   CHECK_RHYDB_QUERY(
      string_column.metadata->phylo_tree.has_value(),
      "Phylotree filter cannot be called on Column '{}' as it does not have a phylogenetic tree "
      "associated with it",
      string_column.metadata->column_name
   );
   auto internal_tree_node = string_column.metadata->phylo_tree->getTreeNodeId(internal_node);
   CHECK_RHYDB_QUERY(
      internal_tree_node.has_value(),
      "The node '{}' does not exist in the phylogenetic tree of column '{}'",
      internal_node,
      string_column.metadata->column_name
   );
   return std::make_unique<filter::operators::BitmapProducer>(
      [&string_column, internal_tree_node] {
         roaring::Roaring result_bitmap = string_column.getDescendants(internal_tree_node.value());
         return Bitmap(std::move(result_bitmap));
      },
      std::move(row_layout)
   );
}

}  // namespace

std::unique_ptr<ScalarExpression> PhyloChildFilter::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<PhyloChildFilter>(column, internal_node);
}

std::unique_ptr<filter::operators::Operator> PhyloChildFilter::compile(const storage::Table& table
) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.hasColumn<storage::column::StringColumn>(column.name),
      "The column '{}' is not of type string",
      column.name
   );

   RHYDB_ASSERT(table.hasColumn<storage::column::StringColumn>(column.name));
   const auto& string_column = table.getColumn<storage::column::StringColumn>(column.name);
   return createMatchingBitmap(string_column, internal_node, table.row_layout);
}

}  // namespace rhydb::query_engine::scalar_expressions
