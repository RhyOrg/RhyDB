#include "rhydb/query_engine/saneql/ast_to_query.h"

#include <map>
#include <memory>

#include <gtest/gtest.h>

#include "rhydb/query_engine/operators/unresolved_phylo_subtree_node.h"
#include "rhydb/query_engine/saneql/parser.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::saneql::parseAndConvertToQueryTree;
namespace operators = rhydb::query_engine::operators;

namespace {

using Tables = std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>>;

Tables makeTablesWithDefault() {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   Tables tables;
   const rhydb::schema::TableName table_name("default");
   tables[table_name] = std::make_shared<rhydb::storage::Table>(table_name, schema);
   return tables;
}

TEST(PhyloSubtree, contractUnaryNodesDefaultsToTrue) {
   auto tables = makeTablesWithDefault();
   auto node = parseAndConvertToQueryTree("default.phyloSubtree(column:='id')", tables);
   const auto* phylo = dynamic_cast<const operators::UnresolvedPhyloSubtreeNode*>(node.get());
   ASSERT_NE(phylo, nullptr);
   EXPECT_TRUE(phylo->contract_unary_nodes);
}

TEST(PhyloSubtree, contractUnaryNodesCanBeSetToFalse) {
   auto tables = makeTablesWithDefault();
   auto node = parseAndConvertToQueryTree(
      "default.phyloSubtree(column:='id', contractUnaryNodes:=false)", tables
   );
   const auto* phylo = dynamic_cast<const operators::UnresolvedPhyloSubtreeNode*>(node.get());
   ASSERT_NE(phylo, nullptr);
   EXPECT_FALSE(phylo->contract_unary_nodes);
}

}  // namespace
