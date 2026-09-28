#include "rhydb/query_engine/optimizer/select_k_rewrite_pass.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/order_by_with_limit_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/order_by_field.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::OrderByField;
using rhydb::query_engine::optimizer::SelectKRewritePass;
namespace operators = rhydb::query_engine::operators;
namespace scalar_expressions = rhydb::query_engine::scalar_expressions;

using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;

namespace {

std::shared_ptr<rhydb::storage::Table> makeTable() {
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   return std::make_shared<rhydb::storage::Table>(rhydb::schema::TableName("default"), schema);
}

operators::QueryNodePtr makeScan() {
   return std::make_unique<operators::TableScanNode>(
      makeTable(),
      std::make_unique<scalar_expressions::BoolLiteral>(true),
      std::vector<ColumnIdentifier>{}
   );
}

std::vector<OrderByField> idAscending() {
   return {OrderByField{.field = {.name = "id", .type = ColumnType::STRING}, .ascending = true}};
}

operators::QueryNodePtr makeOrderBy(
   std::vector<OrderByField> fields,
   std::optional<uint32_t> randomize_seed = std::nullopt
) {
   return std::make_unique<operators::OrderByNode>(makeScan(), std::move(fields), randomize_seed);
}

}  // namespace

TEST(SelectKRewritePass, combinesFetchAboveOrderByIntoOrderByWithLimit) {
   auto fetch = std::make_unique<operators::FetchNode>(makeOrderBy(idAscending()), 5, 2);

   auto result = SelectKRewritePass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::ORDER_BY_WITH_LIMIT);
   auto* combined = dynamic_cast<operators::OrderByWithLimitNode*>(result.get());
   ASSERT_NE(combined, nullptr);
   EXPECT_EQ(combined->limit, 5);
   EXPECT_EQ(combined->offset, 2);
   ASSERT_EQ(combined->fields.size(), 1);
   EXPECT_EQ(combined->fields.at(0).field.name, "id");
   EXPECT_TRUE(combined->fields.at(0).ascending);
   EXPECT_EQ(combined->child->kind(), operators::NodeKind::TABLE_SCAN);
}

TEST(SelectKRewritePass, combinesFetchWithoutOffset) {
   auto fetch = std::make_unique<operators::FetchNode>(makeOrderBy(idAscending()), 5, std::nullopt);

   auto result = SelectKRewritePass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::ORDER_BY_WITH_LIMIT);
   auto* combined = dynamic_cast<operators::OrderByWithLimitNode*>(result.get());
   EXPECT_EQ(combined->limit, 5);
   EXPECT_EQ(combined->offset, std::nullopt);
}

TEST(SelectKRewritePass, doesNotCombineOffsetOnlyFetch) {
   auto fetch = std::make_unique<operators::FetchNode>(makeOrderBy(idAscending()), std::nullopt, 3);

   auto result = SelectKRewritePass::run(std::move(fetch));

   EXPECT_EQ(result->kind(), operators::NodeKind::FETCH);
}

TEST(SelectKRewritePass, combinesRandomizeOrderBy) {
   auto fetch = std::make_unique<operators::FetchNode>(
      makeOrderBy(std::vector<OrderByField>{}, /*randomize_seed=*/42U), 5, std::nullopt
   );

   auto result = SelectKRewritePass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::ORDER_BY_WITH_LIMIT);
   auto* combined = dynamic_cast<operators::OrderByWithLimitNode*>(result.get());
   ASSERT_NE(combined, nullptr);
   EXPECT_EQ(combined->limit, 5);
   EXPECT_TRUE(combined->fields.empty());
   EXPECT_EQ(combined->randomize_seed, 42U);
}

TEST(SelectKRewritePass, doesNotCombineOrderByWithoutFields) {
   auto fetch = std::make_unique<operators::FetchNode>(
      makeOrderBy(std::vector<OrderByField>{}), 5, std::nullopt
   );

   auto result = SelectKRewritePass::run(std::move(fetch));

   EXPECT_EQ(result->kind(), operators::NodeKind::FETCH);
}

TEST(SelectKRewritePass, doesNotCombineFetchAboveNonOrderBy) {
   auto fetch = std::make_unique<operators::FetchNode>(makeScan(), 5, std::nullopt);

   auto result = SelectKRewritePass::run(std::move(fetch));

   EXPECT_EQ(result->kind(), operators::NodeKind::FETCH);
}
