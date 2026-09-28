#include "rhydb/query_engine/optimizer/map_pullup_pass.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/order_by_field.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/comparison.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::optimizer::MapPullupPass;
namespace operators = rhydb::query_engine::operators;
namespace scalar_expressions = rhydb::query_engine::scalar_expressions;

using rhydb::query_engine::filter::operators::Comparator;
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

std::unique_ptr<scalar_expressions::ScalarExpression> trueFilter() {
   return std::make_unique<scalar_expressions::BoolLiteral>(true);
}

operators::QueryNodePtr makeScan() {
   return std::make_unique<operators::TableScanNode>(
      makeTable(), trueFilter(), std::vector<ColumnIdentifier>{}
   );
}

// MapNode producing a new column `x := 3` on top of `child`.
operators::QueryNodePtr makeMap(operators::QueryNodePtr child) {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "x", .type = ColumnType::INT64},
       .expression = std::make_unique<scalar_expressions::Int64Literal>(3),}
   );
   return std::make_unique<operators::MapNode>(std::move(child), std::move(assignments));
}

// --- FetchNode(MapNode(...)) → MapNode(FetchNode(...)) ---

TEST(MapPullupPass, pullsMapUpThroughFetch) {
   auto fetch = std::make_unique<operators::FetchNode>(makeMap(makeScan()), 5, 2);

   auto result = MapPullupPass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::FETCH);
   auto* moved_fetch = dynamic_cast<operators::FetchNode*>(map->child.get());
   EXPECT_EQ(moved_fetch->count, 5);
   EXPECT_EQ(moved_fetch->offset, 2);
   EXPECT_EQ(moved_fetch->child->kind(), operators::NodeKind::TABLE_SCAN);
}

// An offset-only Fetch (no count) keeps its fields after the Map is pulled above it.

TEST(MapPullupPass, pullsMapUpThroughOffsetOnlyFetch) {
   auto fetch = std::make_unique<operators::FetchNode>(makeMap(makeScan()), std::nullopt, 7);

   auto result = MapPullupPass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::FETCH);
   auto* moved_fetch = dynamic_cast<operators::FetchNode*>(map->child.get());
   EXPECT_FALSE(moved_fetch->count.has_value());
   EXPECT_EQ(moved_fetch->offset, 7);
}

// A MapNode with no assignments is still pulled up (it is a no-op pass-through; the pass
// does not need to special-case it, and ColumnNarrowingPass normally removes such Maps
// before this pass runs).

TEST(MapPullupPass, pullsEmptyMapUpThroughFetch) {
   auto map = std::make_unique<operators::MapNode>(
      makeScan(), std::vector<operators::MapNode::Assignment>{}
   );
   auto fetch = std::make_unique<operators::FetchNode>(std::move(map), 5, std::nullopt);

   auto result = MapPullupPass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* result_map = dynamic_cast<operators::MapNode*>(result.get());
   EXPECT_TRUE(result_map->assignments.empty());
   EXPECT_EQ(result_map->child->kind(), operators::NodeKind::FETCH);
}

// --- A zstd-decompression MapNode (the real motivating case) is pulled above a Fetch ---

TEST(MapPullupPass, pullsDecompressMapUpThroughFetch) {
   const auto seq_column = ColumnIdentifier{.name = "seq", .type = ColumnType::NUCLEOTIDE_SEQUENCE};
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "seq", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::ZstdDecompressScalar>(
          std::make_unique<scalar_expressions::FieldRef>(seq_column), "A"
       ),}
   );
   auto map = std::make_unique<operators::MapNode>(
      std::make_unique<operators::TableScanNode>(
         makeTable(), trueFilter(), std::vector<ColumnIdentifier>{seq_column}
      ),
      std::move(assignments)
   );
   auto fetch = std::make_unique<operators::FetchNode>(std::move(map), 1, std::nullopt);

   auto result = MapPullupPass::run(std::move(fetch));

   // Decompression now runs above the limit, so only the retained row is decompressed.
   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* result_map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(result_map->assignments.size(), 1);
   EXPECT_EQ(
      result_map->assignments.front().expression->kind(),
      scalar_expressions::ZstdDecompressScalar::KIND
   );
   ASSERT_EQ(result_map->child->kind(), operators::NodeKind::FETCH);
   auto* moved_fetch = dynamic_cast<operators::FetchNode*>(result_map->child.get());
   EXPECT_EQ(moved_fetch->child->kind(), operators::NodeKind::TABLE_SCAN);
}

// --- FetchNode(FetchNode(MapNode(...))): the map bubbles up through both ---

TEST(MapPullupPass, bubblesMapUpThroughStackedFetch) {
   auto inner_fetch = std::make_unique<operators::FetchNode>(makeMap(makeScan()), 5, std::nullopt);
   auto outer_fetch = std::make_unique<operators::FetchNode>(std::move(inner_fetch), 3, 1);

   auto result = MapPullupPass::run(std::move(outer_fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::FETCH);
   auto* outer = dynamic_cast<operators::FetchNode*>(map->child.get());
   ASSERT_EQ(outer->child->kind(), operators::NodeKind::FETCH);
   auto* inner = dynamic_cast<operators::FetchNode*>(outer->child.get());
   EXPECT_EQ(inner->child->kind(), operators::NodeKind::TABLE_SCAN);
}

TEST(MapPullupPass, leavesFetchOverNonMapInPlace) {
   auto fetch = std::make_unique<operators::FetchNode>(makeScan(), 5, std::nullopt);

   auto result = MapPullupPass::run(std::move(fetch));

   ASSERT_EQ(result->kind(), operators::NodeKind::FETCH);
   auto* fetch_node = dynamic_cast<operators::FetchNode*>(result.get());
   EXPECT_EQ(fetch_node->count, 5);
   EXPECT_EQ(fetch_node->child->kind(), operators::NodeKind::TABLE_SCAN);
}

// --- MapNode(FetchNode(MapNode(...))): the inner map is pulled above the fetch, which brings it
// directly under the root map; the two adjacent maps are then merged into one, giving
// Map(Fetch(scan)). Both maps produce `x := 3`, so the merged map keeps a single `x` assignment.
// ---

TEST(MapPullupPass, pullsInnerMapUpUnderRootMapAndMerges) {
   auto inner_fetch = std::make_unique<operators::FetchNode>(makeMap(makeScan()), 5, std::nullopt);
   auto root_map = makeMap(std::move(inner_fetch));

   auto result = MapPullupPass::run(std::move(root_map));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* merged_map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(merged_map->assignments.size(), 1);
   EXPECT_EQ(merged_map->assignments.front().output_column.name, "x");
   ASSERT_EQ(merged_map->child->kind(), operators::NodeKind::FETCH);
   auto* fetch = dynamic_cast<operators::FetchNode*>(merged_map->child.get());
   EXPECT_EQ(fetch->child->kind(), operators::NodeKind::TABLE_SCAN);
}

// --- Two stacked maps are contracted into one: the upper `at` over the lower in-place decompress
// becomes a single map whose `symbol` assignment holds the nested at(zstdDecompress(seq)). The
// lower `seq` decompression is kept as a passthrough (the upper map does not overwrite it). ---

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MapPullupPass, mergesAtOverDecompressIntoOneMap) {
   const auto seq_column = ColumnIdentifier{.name = "seq", .type = ColumnType::NUCLEOTIDE_SEQUENCE};

   std::vector<operators::MapNode::Assignment> lower_assignments;
   lower_assignments.push_back(
      {.output_column = {.name = "seq", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::ZstdDecompressScalar>(
          std::make_unique<scalar_expressions::FieldRef>(seq_column), "A"
       ),}
   );
   auto lower_map = std::make_unique<operators::MapNode>(
      std::make_unique<operators::TableScanNode>(
         makeTable(), trueFilter(), std::vector<ColumnIdentifier>{seq_column}
      ),
      std::move(lower_assignments)
   );

   std::vector<operators::MapNode::Assignment> upper_assignments;
   upper_assignments.push_back(
      {.output_column = {.name = "symbol", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::At>(
          std::make_unique<scalar_expressions::FieldRef>(
             ColumnIdentifier{.name = "seq", .type = ColumnType::STRING}
          ),
          3
       ),}
   );
   auto upper_map =
      std::make_unique<operators::MapNode>(std::move(lower_map), std::move(upper_assignments));

   auto result = MapPullupPass::run(std::move(upper_map));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* merged = dynamic_cast<operators::MapNode*>(result.get());
   EXPECT_EQ(merged->child->kind(), operators::NodeKind::TABLE_SCAN);

   const auto symbol = std::ranges::find_if(merged->assignments, [](const auto& assignment) {
      return assignment.output_column.name == "symbol";
   });
   ASSERT_NE(symbol, merged->assignments.end());
   const auto* at_expression =
      scalar_expressions::dynCast<scalar_expressions::At>(symbol->expression.get());
   ASSERT_NE(at_expression, nullptr);
   EXPECT_EQ(at_expression->position, 3);
   const auto* decompress = scalar_expressions::dynCast<scalar_expressions::ZstdDecompressScalar>(
      at_expression->input.get()
   );
   ASSERT_NE(decompress, nullptr);
   EXPECT_TRUE(scalar_expressions::isA<scalar_expressions::FieldRef>(decompress->input.get()));
}

// --- The merge safely declines when an upper expression references a lower-produced column through
// a construct it cannot rewrite (here a Comparison predicate): inlining would be needed to avoid a
// dangling reference, and since the merge cannot inline into a Comparison, both maps are left in
// place. ---

TEST(MapPullupPass, doesNotMergeWhenUpperReferencesProducedColumnUnsubstitutably) {
   const auto seq_column = ColumnIdentifier{.name = "seq", .type = ColumnType::NUCLEOTIDE_SEQUENCE};

   std::vector<operators::MapNode::Assignment> lower_assignments;
   lower_assignments.push_back(
      {.output_column = {.name = "seq", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::ZstdDecompressScalar>(
          std::make_unique<scalar_expressions::FieldRef>(seq_column), "A"
       ),}
   );
   auto lower_map = std::make_unique<operators::MapNode>(
      std::make_unique<operators::TableScanNode>(
         makeTable(), trueFilter(), std::vector<ColumnIdentifier>{seq_column}
      ),
      std::move(lower_assignments)
   );

   // `flag := (seq = "AAAA")` reads the lower-produced `seq` through a Comparison the merge cannot
   // rewrite, so it must decline rather than leave a reference to a column the merged map no longer
   // produces.
   std::vector<operators::MapNode::Assignment> upper_assignments;
   upper_assignments.push_back(
      {.output_column = {.name = "flag", .type = ColumnType::BOOL},
       .expression = std::make_unique<scalar_expressions::Comparison>(
          std::make_unique<scalar_expressions::FieldRef>(
             ColumnIdentifier{.name = "seq", .type = ColumnType::STRING}
          ),
          std::make_unique<scalar_expressions::StringLiteral>("AAAA"),
          Comparator::EQUALS
       ),}
   );
   auto upper_map =
      std::make_unique<operators::MapNode>(std::move(lower_map), std::move(upper_assignments));

   auto result = MapPullupPass::run(std::move(upper_map));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* upper = dynamic_cast<operators::MapNode*>(result.get());
   EXPECT_EQ(upper->child->kind(), operators::NodeKind::MAP);
}

// Blocked: FilterNode. Should be handled by the filter pushdown

TEST(MapPullupPass, doesNotPullMapUpThroughFilter) {
   auto filter = std::make_unique<operators::FilterNode>(makeMap(makeScan()), trueFilter());

   auto result = MapPullupPass::run(std::move(filter));

   ASSERT_EQ(result->kind(), operators::NodeKind::FILTER);
   auto* filter_node = dynamic_cast<operators::FilterNode*>(result.get());
   EXPECT_EQ(filter_node->child->kind(), operators::NodeKind::MAP);
}

// --- Blocked: ProjectNode is the root over the MapNode ---

TEST(MapPullupPass, doesNotPullMapUpThroughProject) {
   auto project = std::make_unique<operators::ProjectNode>(
      makeMap(makeScan()), std::vector<ColumnIdentifier>{{.name = "x", .type = ColumnType::INT64}}
   );

   auto result = MapPullupPass::run(std::move(project));

   ASSERT_EQ(result->kind(), operators::NodeKind::PROJECT);
   auto* project_node = dynamic_cast<operators::ProjectNode*>(result.get());
   EXPECT_EQ(project_node->child->kind(), operators::NodeKind::MAP);
}

// --- OrderByNode(MapNode(...)) → MapNode(OrderByNode(...)) when the order-by fields do not
// reference a produced column ---

TEST(MapPullupPass, pullsMapUpThroughOrderBy) {
   std::vector<rhydb::query_engine::OrderByField> fields{
      {.field = {.name = "id", .type = ColumnType::STRING}, .ascending = true},
   };
   auto order_by = std::make_unique<operators::OrderByNode>(
      makeMap(makeScan()), std::move(fields), std::nullopt
   );

   auto result = MapPullupPass::run(std::move(order_by));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::ORDER_BY);
   auto* moved_order_by = dynamic_cast<operators::OrderByNode*>(map->child.get());
   ASSERT_EQ(moved_order_by->fields.size(), 1);
   EXPECT_EQ(moved_order_by->fields.front().field.name, "id");
   EXPECT_TRUE(moved_order_by->fields.front().ascending);
   EXPECT_EQ(moved_order_by->child->kind(), operators::NodeKind::TABLE_SCAN);
}

// The randomize seed is preserved when the OrderBy moves below the Map.

TEST(MapPullupPass, pullsMapUpThroughOrderByPreservingRandomizeSeed) {
   std::vector<rhydb::query_engine::OrderByField> fields{
      {.field = {.name = "id", .type = ColumnType::STRING}, .ascending = true},
   };
   auto order_by = std::make_unique<operators::OrderByNode>(
      makeMap(makeScan()), std::move(fields), std::optional<uint32_t>{42}
   );

   auto result = MapPullupPass::run(std::move(order_by));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* map = dynamic_cast<operators::MapNode*>(result.get());
   ASSERT_EQ(map->child->kind(), operators::NodeKind::ORDER_BY);
   auto* moved_order_by = dynamic_cast<operators::OrderByNode*>(map->child.get());
   ASSERT_TRUE(moved_order_by->randomize_seed.has_value());
   EXPECT_EQ(moved_order_by->randomize_seed.value(), 42);
}

// --- Blocked: an order-by field references a column the MapNode produces, so swapping would
// change ordering. The MapNode stays below the OrderBy. ---

TEST(MapPullupPass, doesNotPullMapUpThroughOrderByOnProducedColumn) {
   // makeMap produces column `x`; ordering by `x` must not be pushed below the Map.
   std::vector<rhydb::query_engine::OrderByField> fields{
      {.field = {.name = "x", .type = ColumnType::INT64}, .ascending = true},
   };
   auto order_by = std::make_unique<operators::OrderByNode>(
      makeMap(makeScan()), std::move(fields), std::nullopt
   );

   auto result = MapPullupPass::run(std::move(order_by));

   ASSERT_EQ(result->kind(), operators::NodeKind::ORDER_BY);
   auto* order_by_node = dynamic_cast<operators::OrderByNode*>(result.get());
   EXPECT_EQ(order_by_node->child->kind(), operators::NodeKind::MAP);
}

// --- Blocked: AggregateNode ---

TEST(MapPullupPass, doesNotPullMapUpThroughAggregate) {
   auto aggregate = std::make_unique<operators::AggregateNode>(
      makeMap(makeScan()),
      std::vector<ColumnIdentifier>{},
      std::vector<operators::AggregateDefinition>{
         {.output_name = "count", .function = operators::AggregateFunction::COUNT},
      }
   );

   auto result = MapPullupPass::run(std::move(aggregate));

   ASSERT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
   auto* aggregate_node = dynamic_cast<operators::AggregateNode*>(result.get());
   EXPECT_EQ(aggregate_node->child->kind(), operators::NodeKind::MAP);
}

// --- Blocked: a MapNode at the root is left untouched ---

TEST(MapPullupPass, leavesRootMapInPlace) {
   auto map = makeMap(makeScan());

   auto result = MapPullupPass::run(std::move(map));

   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* root_map = dynamic_cast<operators::MapNode*>(result.get());
   EXPECT_EQ(root_map->child->kind(), operators::NodeKind::TABLE_SCAN);
}

}  // namespace
