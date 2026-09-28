#include "rhydb/query_engine/optimizer/filter_pushdown_pass.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <arrow/acero/options.h>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/join_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/order_by_with_limit_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/union_all_node.h"
#include "rhydb/query_engine/operators/unresolved_mutations_node.h"
#include "rhydb/query_engine/order_by_field.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::optimizer::FilterPushdownPass;
namespace operators = rhydb::query_engine::operators;
namespace scalar_expressions = rhydb::query_engine::scalar_expressions;

namespace {

std::shared_ptr<rhydb::storage::Table> makeTable() {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   return std::make_shared<rhydb::storage::Table>(rhydb::schema::TableName("default"), schema);
}

std::unique_ptr<scalar_expressions::ScalarExpression> makeDummyFilter() {
   return std::make_unique<scalar_expressions::BoolLiteral>(true);
}

operators::QueryNodePtr makeScan() {
   return std::make_unique<operators::TableScanNode>(
      makeTable(), makeDummyFilter(), std::vector<rhydb::schema::ColumnIdentifier>{}
   );
}

operators::QueryNodePtr makeFilteredScan(bool filter_value) {
   return std::make_unique<operators::FilterNode>(
      makeScan(), std::make_unique<scalar_expressions::BoolLiteral>(filter_value)
   );
}

// --- FilterNode(TableScanNode) ---

TEST(FilterPushdownPass, eliminatesFilterNodeAboveTableScan) {
   auto table = makeTable();
   auto scan = std::make_unique<operators::TableScanNode>(
      table, makeDummyFilter(), std::vector<rhydb::schema::ColumnIdentifier>{}
   );
   auto filter_node = std::make_unique<operators::FilterNode>(std::move(scan), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   // The FilterNode is gone; result is the TableScanNode directly.
   EXPECT_EQ(result->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(result.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true & true)");
}

// --- FilterNode(FilterNode(TableScanNode)) ---
TEST(FilterPushdownPass, eliminatesStackedFilterNodesAboveTableScan) {
   auto inner_filter = makeFilteredScan(false);
   auto outer_filter =
      std::make_unique<operators::FilterNode>(std::move(inner_filter), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(outer_filter));

   // Both FilterNodes are gone; result is the TableScanNode with all three filters merged.
   EXPECT_EQ(result->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(result.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true & false & true)");
}

// --- MapNode(FilterNode(TableScanNode)) ---

TEST(FilterPushdownPass, pushesFilterThroughMapIntoTableScan) {
   auto table = makeTable();
   auto scan = std::make_unique<operators::TableScanNode>(
      table, makeDummyFilter(), std::vector<rhydb::schema::ColumnIdentifier>{}
   );
   auto filter_node = std::make_unique<operators::FilterNode>(std::move(scan), makeDummyFilter());

   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "x", .type = rhydb::schema::ColumnType::INT64},
       .expression = std::make_unique<scalar_expressions::Int64Literal>(3),}
   );
   auto map_node =
      std::make_unique<operators::MapNode>(std::move(filter_node), std::move(assignments));

   auto result = FilterPushdownPass::run(std::move(map_node));

   // The MapNode is retained; the FilterNode below it was pushed into the TableScan.
   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true & true)");
}

// --- FilterNode(MapNode(TableScanNode)): the swap that keeps decompression above the filter ---
//
// This is the motivating case for #1343: a filter stacked on top of a (decompression) MapNode.
// FilterPushdownPass keeps the MapNode on top and pushes the filter down into the TableScan,
// so only the rows matching the filter are ever decompressed.
TEST(FilterPushdownPass, pushesFilterThroughDecompressMapIntoTableScan) {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;

   const ColumnIdentifier seq_column{.name = "seq", .type = ColumnType::NUCLEOTIDE_SEQUENCE};
   auto scan = std::make_unique<operators::TableScanNode>(
      makeTable(), makeDummyFilter(), std::vector<ColumnIdentifier>{seq_column}
   );

   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "seq", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::ZstdDecompressScalar>(
          std::make_unique<scalar_expressions::FieldRef>(seq_column), "A"
       ),}
   );
   auto map_node = std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(map_node), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   // The FilterNode is gone; the decompression MapNode stays on top with the filter pushed
   // into the TableScan below it.
   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->assignments.size(), 1);
   EXPECT_EQ(
      map->assignments.front().expression->kind(), scalar_expressions::ZstdDecompressScalar::KIND
   );
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true & true)");
}

// --- FilterNode(MapNode(TableScanNode)) referencing a map-produced column ---
//
// Regression test for #1371: a filter that references a column produced by the map (here `x`)
// must NOT be pushed below the map, since `x` does not exist beneath it. The optimizer must not
// turn this valid plan into an invalid one; the FilterNode is kept above the MapNode instead.
TEST(FilterPushdownPass, doesNotPushFilterReferencingMapProducedColumnBelowMap) {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;

   auto table = makeTable();
   auto scan = std::make_unique<operators::TableScanNode>(
      table, makeDummyFilter(), std::vector<ColumnIdentifier>{}
   );

   const ColumnIdentifier produced_column{.name = "x", .type = ColumnType::INT64};
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = produced_column,
       .expression = std::make_unique<scalar_expressions::Int64Literal>(3),}
   );
   auto map_node = std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   // filter references the map-produced column `x`
   auto filter_node = std::make_unique<operators::FilterNode>(
      std::move(map_node), std::make_unique<scalar_expressions::FieldRef>(produced_column)
   );

   auto result = FilterPushdownPass::run(std::move(filter_node));

   // The FilterNode stays on top of the MapNode; nothing was pushed into the TableScan.
   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(x)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(filter->child.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   // Only the scan's own filter remains; the outer filter was not pushed in.
   EXPECT_EQ(table_scan->filter->toString(), "And(true)");
}

// A filter that references only columns the map passes through (not any map-produced column)
// is still pushed below the map into the TableScan.
TEST(FilterPushdownPass, pushesFilterReferencingPassThroughColumnBelowMap) {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;

   auto table = makeTable();
   auto scan = std::make_unique<operators::TableScanNode>(
      table, makeDummyFilter(), std::vector<ColumnIdentifier>{}
   );

   const ColumnIdentifier produced_column{.name = "x", .type = ColumnType::INT64};
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = produced_column,
       .expression = std::make_unique<scalar_expressions::Int64Literal>(3),}
   );
   auto map_node = std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   // filter references `id`, a pass-through column the map does not produce
   const ColumnIdentifier pass_through_column{.name = "id", .type = ColumnType::STRING};
   auto filter_node = std::make_unique<operators::FilterNode>(
      std::move(map_node), std::make_unique<scalar_expressions::FieldRef>(pass_through_column)
   );

   auto result = FilterPushdownPass::run(std::move(filter_node));

   // The MapNode is retained; the filter was pushed below it into the TableScan.
   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(id & true)");
}

// A filter referencing a column produced by a pushdown-transparent assignment (a zstd
// decompression of the same-named column) IS pushed below the map into the TableScan, where the
// filter compiles against the physical column. This is the shape the compiler inserts for
// `segment1 := zstd_decompress(segment1)`; the co-occurrence-with-filter query relies on it.
TEST(FilterPushdownPass, pushesFilterThroughTransparentDecompressMapIntoTableScan) {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;

   const ColumnIdentifier compressed_column{.name = "seq", .type = ColumnType::NUCLEOTIDE_SEQUENCE};
   auto scan = std::make_unique<operators::TableScanNode>(
      makeTable(), makeDummyFilter(), std::vector<ColumnIdentifier>{compressed_column}
   );

   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "seq", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::ZstdDecompressScalar>(
          std::make_unique<scalar_expressions::FieldRef>(compressed_column), "A"
       ),}
   );
   auto map_node = std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   // filter references the decompressed column `seq` {seq, STRING} - same identifier the map
   // produces, but the assignment is a transparent realization so the filter can be pushed down.
   const ColumnIdentifier decompressed_column{.name = "seq", .type = ColumnType::STRING};
   auto filter_node = std::make_unique<operators::FilterNode>(
      std::move(map_node), std::make_unique<scalar_expressions::FieldRef>(decompressed_column)
   );

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(seq & true)");
}

// A filter referencing a column that a map REPLACES in place with a value-changing (non
// transparent) expression must NOT be pushed below the map. Pushing it into the scan would
// filter on the original physical value instead of the map's computed one - a silent miscompile.
// This mirrors e.g. `map({primaryKey := primaryKey.at(1)}).filter(primaryKey = 'i')`; here a
// literal assignment stands in for any non-transparent expression.
TEST(FilterPushdownPass, doesNotPushFilterThroughValueChangingReplaceInPlaceMap) {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;

   auto scan = std::make_unique<operators::TableScanNode>(
      makeTable(), makeDummyFilter(), std::vector<ColumnIdentifier>{}
   );

   // replace the existing pass-through column `id` with a computed value
   const ColumnIdentifier replaced_column{.name = "id", .type = ColumnType::INT64};
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = replaced_column,
       .expression = std::make_unique<scalar_expressions::Int64Literal>(3),}
   );
   auto map_node = std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   auto filter_node = std::make_unique<operators::FilterNode>(
      std::move(map_node), std::make_unique<scalar_expressions::FieldRef>(replaced_column)
   );

   auto result = FilterPushdownPass::run(std::move(filter_node));

   // The FilterNode is retained above the map; nothing was pushed into the TableScan.
   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(filter->child.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true)");
}

// --- FilterNode(ProjectNode(MapNode(FilterNode(TableScanNode)))) ---
TEST(FilterPushdownPass, pushesFilterThroughProjectAndMapIntoTableScan) {
   auto inner_filter = makeFilteredScan(false);

   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "x", .type = rhydb::schema::ColumnType::INT64},
       .expression = std::make_unique<scalar_expressions::Int64Literal>(3),}
   );
   auto map_node =
      std::make_unique<operators::MapNode>(std::move(inner_filter), std::move(assignments));
   auto project_node = std::make_unique<operators::ProjectNode>(
      std::move(map_node),
      std::vector<rhydb::schema::ColumnIdentifier>{
         {.name = "x", .type = rhydb::schema::ColumnType::INT64},
      }
   );
   auto outer_filter =
      std::make_unique<operators::FilterNode>(std::move(project_node), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(outer_filter));

   ASSERT_EQ(result->kind(), operators::NodeKind::PROJECT);
   auto* project = dynamic_cast<operators::ProjectNode*>(result.get());
   ASSERT_EQ(project->child->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(project->child.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(map->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true & false & true)");
}

// --- FilterNode(OrderByNode(...)) ---

// A deterministic orderBy sorts by column values (stable on the input's relative order, which
// filtering preserves), so a filter above it is pushed down into the scan.
TEST(FilterPushdownPass, pushesFilterThroughDeterministicOrderByIntoTableScan) {
   std::vector<rhydb::query_engine::OrderByField> fields;
   fields.push_back(
      {.field = {.name = "primaryKey", .type = rhydb::schema::ColumnType::STRING}, .ascending = true,
      }
   );
   auto order_by = std::make_unique<operators::OrderByNode>(
      makeFilteredScan(false), std::move(fields), std::nullopt
   );
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(order_by), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::ORDER_BY);
   auto* order = dynamic_cast<operators::OrderByNode*>(result.get());
   ASSERT_EQ(order->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(order->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(true & false & true)");
}

// A randomized orderBy derives its sort keys from input ordinals, so a filter above it must be
// retained above it - pushing it below would reindex the surviving rows and change the order.
TEST(FilterPushdownPass, retainsFilterAboveRandomizedOrderBy) {
   auto order_by = std::make_unique<operators::OrderByNode>(
      makeScan(), std::vector<rhydb::query_engine::OrderByField>{}, std::optional<uint32_t>{0}
   );
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(order_by), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(true)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::ORDER_BY);
   auto* order = dynamic_cast<operators::OrderByNode*>(filter->child.get());
   ASSERT_EQ(order->child->kind(), operators::NodeKind::TABLE_SCAN);
}

// --- FilterNode(UnionAllNode(FilterNode(TableScanNode), FilterNode(TableScanNode))) ---
TEST(FilterPushdownPass, pushesFilterIntoBothUnionAllBranches) {
   // left branch: Filter(false, Scan), right branch: Filter(true, Scan)
   auto union_all =
      std::make_unique<operators::UnionAllNode>(makeFilteredScan(false), makeFilteredScan(true));
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(union_all), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::UNION_ALL);
   auto* union_node = dynamic_cast<operators::UnionAllNode*>(result.get());
   ASSERT_EQ(union_node->left->kind(), operators::NodeKind::TABLE_SCAN);
   ASSERT_EQ(union_node->right->kind(), operators::NodeKind::TABLE_SCAN);
   auto* left_scan = dynamic_cast<operators::TableScanNode*>(union_node->left.get());
   auto* right_scan = dynamic_cast<operators::TableScanNode*>(union_node->right.get());
   // outer filter (true) + branch filter (false/true) + scan filter (true)
   EXPECT_EQ(left_scan->filter->toString(), "And(true & false & true)");
   EXPECT_EQ(right_scan->filter->toString(), "And(true & true & true)");
}

// --- FilterNode(JoinNode(...)) ---

operators::QueryNodePtr makeJoin(operators::QueryNodePtr left, operators::QueryNodePtr right) {
   return std::make_unique<operators::JoinNode>(
      std::move(left),
      std::move(right),
      std::vector<rhydb::schema::ColumnIdentifier>{},
      std::vector<rhydb::schema::ColumnIdentifier>{},
      arrow::acero::JoinType::INNER
   );
}

// A filter sitting above a join cannot be pushed into a single input safely, so it is left in
// place above the join and later realized as an Arrow filter over the join output rather than
// being mis-attributed to one side.
TEST(FilterPushdownPass, retainsFilterAboveJoin) {
   auto join = makeJoin(makeScan(), makeScan());
   auto filter_node = std::make_unique<operators::FilterNode>(std::move(join), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   // The FilterNode stays on top of the JoinNode; nothing was pushed into either input.
   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(true)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::JOIN);
   auto* join_node = dynamic_cast<operators::JoinNode*>(filter->child.get());
   ASSERT_EQ(join_node->left->kind(), operators::NodeKind::TABLE_SCAN);
   ASSERT_EQ(join_node->right->kind(), operators::NodeKind::TABLE_SCAN);
}

// Filters that live *inside* a join input are still pushed down into that input's scan;
// filters stacked on top of the join itself are left above the join.
TEST(FilterPushdownPass, pushesFiltersInsideJoinInputsIntoScans) {
   auto join = makeJoin(makeFilteredScan(false), makeScan());

   auto result = FilterPushdownPass::run(std::move(join));

   ASSERT_EQ(result->kind(), operators::NodeKind::JOIN);
   auto* join_node = dynamic_cast<operators::JoinNode*>(result.get());
   ASSERT_EQ(join_node->left->kind(), operators::NodeKind::TABLE_SCAN);
   ASSERT_EQ(join_node->right->kind(), operators::NodeKind::TABLE_SCAN);
   auto* left_scan = dynamic_cast<operators::TableScanNode*>(join_node->left.get());
   auto* right_scan = dynamic_cast<operators::TableScanNode*>(join_node->right.get());
   // left: branch filter (false) + scan filter (true); right: only its scan filter (true)
   EXPECT_EQ(left_scan->filter->toString(), "And(false & true)");
   EXPECT_EQ(right_scan->filter->toString(), "And(true)");
}

// --- FilterNode(UnresolvedMutationsNode(FilterNode(TableScanNode))) ---
//
// mutations() produces a new result schema (proportion, count, position, ...); the input columns
// are gone above it. A filter above it references those output columns and must be retained above
// (later realized as an Arrow filter over the mutations output), NOT pushed into the input scan
// where those columns do not exist. The pre-filter written below mutations still reaches the scan
// so node resolution finds a bare table scan.
TEST(FilterPushdownPass, retainsFilterAboveMutationsWhilePushingChildFilters) {
   auto mutations = std::make_unique<operators::UnresolvedMutationsNode<rhydb::Nucleotide>>(
      makeFilteredScan(false), std::vector<std::string>{}, 0.0, std::vector<std::string>{}
   );
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(mutations), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(true)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::UNRESOLVED_MUTATIONS_NUCLEOTIDE);
   auto* mutations_node =
      dynamic_cast<operators::UnresolvedMutationsNode<rhydb::Nucleotide>*>(filter->child.get());
   ASSERT_EQ(mutations_node->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(mutations_node->child.get());
   // The child-internal (pre-)filter (false) plus the scan's own filter (true) reached the scan;
   // the above-filter (true) stayed above the mutations node.
   EXPECT_EQ(table_scan->filter->toString(), "And(false & true)");
}

// --- FilterNode(FetchNode(FilterNode(TableScanNode))) ---
//
// A FetchNode (limit/offset) changes which rows survive, so a filter above it must NOT be pushed
// below it (`limit(1).filter(...)` must filter the single limited row, not pre-filter and then
// limit). The outer filter is retained above the fetch; a filter living inside the fetch's child
// subtree is still pushed into the scan.
TEST(FilterPushdownPass, retainsFilterAboveFetchWhilePushingChildFilters) {
   auto fetch = std::make_unique<operators::FetchNode>(
      makeFilteredScan(false), std::optional<uint32_t>{1}, std::nullopt
   );
   auto filter_node = std::make_unique<operators::FilterNode>(std::move(fetch), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(true)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::FETCH);
   auto* fetch_node = dynamic_cast<operators::FetchNode*>(filter->child.get());
   ASSERT_EQ(fetch_node->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(fetch_node->child.get());
   // The child-internal filter (false) and the scan's own filter (true) were merged into the scan.
   EXPECT_EQ(table_scan->filter->toString(), "And(false & true)");
}

// --- FilterNode(OrderByWithLimitNode(FilterNode(TableScanNode))) ---
//
// A top-k node keeps only the `offset + limit` smallest rows, so like FetchNode a filter above it
// must be retained rather than pushed below it. Child-internal filters still reach the scan.
TEST(FilterPushdownPass, retainsFilterAboveOrderByWithLimitWhilePushingChildFilters) {
   auto order_limit = std::make_unique<operators::OrderByWithLimitNode>(
      makeFilteredScan(false),
      std::vector<rhydb::query_engine::OrderByField>{},
      uint32_t{1},
      std::nullopt,
      std::nullopt
   );
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(order_limit), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(true)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::ORDER_BY_WITH_LIMIT);
   auto* order_node = dynamic_cast<operators::OrderByWithLimitNode*>(filter->child.get());
   ASSERT_EQ(order_node->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(order_node->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(false & true)");
}

// --- FilterNode(AggregateNode(FilterNode(TableScanNode))) ---
//
// An aggregate produces a new schema (group-by keys and aggregate outputs such as `count`) that
// does not exist below it, so a filter referencing those columns must be retained above the
// aggregate and realized as an Arrow filter over its output, not pushed into the scan. Filters
// inside the aggregate's input subtree are still pushed down.
TEST(FilterPushdownPass, retainsFilterAboveAggregateWhilePushingChildFilters) {
   auto aggregate = std::make_unique<operators::AggregateNode>(
      makeFilteredScan(false),
      std::vector<rhydb::schema::ColumnIdentifier>{},
      std::vector<operators::AggregateDefinition>{}
   );
   auto filter_node =
      std::make_unique<operators::FilterNode>(std::move(aggregate), makeDummyFilter());

   auto result = FilterPushdownPass::run(std::move(filter_node));

   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter->filter->toString(), "And(true)");
   ASSERT_EQ(filter->child->kind(), operators::NodeKind::AGGREGATE);
   auto* aggregate_node = dynamic_cast<operators::AggregateNode*>(filter->child.get());
   ASSERT_EQ(aggregate_node->child->kind(), operators::NodeKind::TABLE_SCAN);
   auto* table_scan = dynamic_cast<operators::TableScanNode*>(aggregate_node->child.get());
   EXPECT_EQ(table_scan->filter->toString(), "And(false & true)");
}

}  // namespace
