#include "rhydb/query_engine/optimizer/column_narrowing_pass.h"

#include <memory>
#include <optional>
#include <vector>

#include "rhydb/query_engine/operator_visitor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/fetch_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/order_by_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/union_all_node.h"
#include "rhydb/query_engine/order_by_field.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::optimizer::ColumnNarrowingPass;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
namespace operators = rhydb::query_engine::operators;

namespace {

ColumnIdentifier col(std::string name) {
   return ColumnIdentifier{.name = std::move(name), .type = ColumnType::STRING};
}

ColumnIdentifier colWithType(std::string name, ColumnType type) {
   return ColumnIdentifier{.name = std::move(name), .type = type};
}

std::shared_ptr<rhydb::storage::Table> dummyTable() {
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key = col("id");
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   return std::make_shared<rhydb::storage::Table>(rhydb::schema::TableName::getDefault(), schema);
}

// The narrowing pass only inspects the scan's field list, so the table identity is irrelevant.
std::unique_ptr<operators::TableScanNode> makeScan(std::vector<ColumnIdentifier> fields) {
   return std::make_unique<operators::TableScanNode>(
      dummyTable(),
      std::make_unique<rhydb::query_engine::scalar_expressions::BoolLiteral>(true),
      std::move(fields)
   );
}

std::vector<ColumnIdentifier> scanSchema(const operators::TableScanNode& node) {
   return node.fields;
}

// Extracts the TableScanNode from the bottom of a chain of unary nodes.
// NOLINTNEXTLINE(misc-no-recursion)
operators::TableScanNode& leafScan(operators::QueryNode& node) {
   if (auto* scan = dynamic_cast<operators::TableScanNode*>(&node)) {
      return *scan;
   }
   if (auto* proj = dynamic_cast<operators::ProjectNode*>(&node)) {
      return leafScan(*proj->child);
   }
   if (auto* fetch = dynamic_cast<operators::FetchNode*>(&node)) {
      return leafScan(*fetch->child);
   }
   if (auto* order = dynamic_cast<operators::OrderByNode*>(&node)) {
      return leafScan(*order->child);
   }
   if (auto* agg = dynamic_cast<operators::AggregateNode*>(&node)) {
      return leafScan(*agg->child);
   }
   if (auto* flt = dynamic_cast<operators::FilterNode*>(&node)) {
      return leafScan(*flt->child);
   }
   if (auto* map = dynamic_cast<operators::MapNode*>(&node)) {
      return leafScan(*map->child);
   }
   throw std::logic_error("unexpected node type in leafScan");
}

// Builds a MapNode assignment for decompressing a sequence column into a STRING column.
operators::MapNode::Assignment decompressAssignment(const ColumnIdentifier& sequence_col) {
   return {
      .output_column = col(sequence_col.name),
      .expression = std::make_unique<rhydb::query_engine::scalar_expressions::ZstdDecompressScalar>(
         std::make_unique<rhydb::query_engine::scalar_expressions::FieldRef>(sequence_col), "A"
      ),
   };
}

// Assignment holds a move-only expression, so it cannot be brace-initialized into a
// vector (that would copy). This wraps a single assignment into a vector by moving.
std::vector<operators::MapNode::Assignment> decompressAssignments(
   const ColumnIdentifier& sequence_col
) {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(decompressAssignment(sequence_col));
   return assignments;
}

// --- TableScanNode ---

TEST(ColumnNarrowingPassScan, keepsOnlyRequiredColumns) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   ColumnNarrowingPass pass({col("b")});
   pass(*scan);

   EXPECT_THAT(scanSchema(*scan), ::testing::ElementsAre(col("b")));
}

TEST(ColumnNarrowingPassScan, removesAllWhenNoneRequired) {
   auto scan = makeScan({col("a"), col("b")});
   ColumnNarrowingPass pass({});
   pass(*scan);

   EXPECT_THAT(scanSchema(*scan), ::testing::IsEmpty());
}

TEST(ColumnNarrowingPassScan, keepsAllWhenAllRequired) {
   auto scan = makeScan({col("x"), col("y")});
   ColumnNarrowingPass pass({col("x"), col("y")});
   pass(*scan);

   EXPECT_THAT(scanSchema(*scan), ::testing::ElementsAre(col("x"), col("y")));
}

// --- ProjectNode -> TableScanNode ---

TEST(ColumnNarrowingPassProject, narrowsScanToProjectedColumns) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   operators::QueryNodePtr node =
      std::make_unique<operators::ProjectNode>(std::move(scan), std::vector{col("a")});

   ColumnNarrowingPass pass({col("a"), col("b"), col("c")});
   if (auto new_node = operators::visit(*node, pass)) {
      node = std::move(new_node);
   }

   EXPECT_THAT(scanSchema(leafScan(*node)), ::testing::ElementsAre(col("a")));
}

// --- AggregateNode -> TableScanNode ---

TEST(ColumnNarrowingPassAggregate, narrowsScanToGroupByColumns) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   auto agg = std::make_unique<operators::AggregateNode>(
      std::move(scan),
      std::vector{col("b")},
      std::vector<operators::AggregateDefinition>{
         {.output_name = "cnt",
          .function = operators::AggregateFunction::COUNT,
          .source_column = std::nullopt,},
      }
   );

   ColumnNarrowingPass pass({col("a"), col("b"), col("c")});
   pass(*agg);

   EXPECT_THAT(scanSchema(leafScan(*agg)), ::testing::ElementsAre(col("b")));
}

TEST(ColumnNarrowingPassAggregate, countStarWithNoGroupByKeepsOneColumn) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   auto agg = std::make_unique<operators::AggregateNode>(
      std::move(scan),
      std::vector<ColumnIdentifier>{},
      std::vector<operators::AggregateDefinition>{
         {.output_name = "cnt",
          .function = operators::AggregateFunction::COUNT,
          .source_column = std::nullopt,},
      }
   );

   ColumnNarrowingPass pass({col("a"), col("b"), col("c")});
   pass(*agg);

   // COUNT(*) with no group-by needs only one column to drive the row stream.
   EXPECT_THAT(scanSchema(leafScan(*agg)), ::testing::SizeIs(1));
}

// --- OrderByNode -> TableScanNode ---

TEST(ColumnNarrowingPassOrderBy, appendsSortKeyToRequired) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   auto order = std::make_unique<operators::OrderByNode>(
      std::move(scan),
      std::vector<rhydb::query_engine::OrderByField>{{.field = col("c"), .ascending = true}},
      std::nullopt
   );

   // Only "a" is required from above; the pass must add "c" for sorting.
   ColumnNarrowingPass pass({col("a")});
   pass(*order);

   EXPECT_THAT(scanSchema(leafScan(*order)), ::testing::UnorderedElementsAre(col("a"), col("c")));
}

// --- ProjectNode elimination ---

TEST(ColumnNarrowingPassProject, collapsesWhenScanNarrowedToProjectFields) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   auto proj =
      std::make_unique<operators::ProjectNode>(std::move(scan), std::vector{col("a"), col("b")});
   auto fetch = std::make_unique<operators::FetchNode>(std::move(proj), std::nullopt, std::nullopt);

   ColumnNarrowingPass pass({col("a"), col("b")});
   pass(*fetch);

   // The ProjectNode should be removed; FetchNode's child is now directly the TableScanNode.
   EXPECT_NE(dynamic_cast<operators::TableScanNode*>(fetch->child.get()), nullptr);
   EXPECT_THAT(scanSchema(leafScan(*fetch)), ::testing::ElementsAre(col("a"), col("b")));
}

TEST(ColumnNarrowingPassProject, stackedProjectsDoNotPropagateSuperfluousColumnsToScan) {
   // Only "a" is needed from outside, but the outer project exposes [a, b] and the inner [a, b, c].
   // The scan should still be narrowed to just [a].
   auto scan = makeScan({col("a"), col("b"), col("c"), col("d")});
   auto inner_proj = std::make_unique<operators::ProjectNode>(
      std::move(scan), std::vector{col("a"), col("b"), col("c")}
   );
   operators::QueryNodePtr outer_proj = std::make_unique<operators::ProjectNode>(
      std::move(inner_proj), std::vector{col("a"), col("b")}
   );

   ColumnNarrowingPass pass({col("a")});
   if (auto new_node = operators::visit(*outer_proj, pass)) {
      outer_proj = std::move(new_node);
   }

   EXPECT_THAT(scanSchema(leafScan(*outer_proj)), ::testing::ElementsAre(col("a")));
}

TEST(ColumnNarrowingPassProject, stackedProjectsAreCollapsedAfterNarrowing) {
   auto scan = makeScan({col("a"), col("b"), col("c"), col("d")});
   auto inner_proj = std::make_unique<operators::ProjectNode>(
      std::move(scan), std::vector{col("a"), col("b"), col("c")}
   );
   auto outer_proj = std::make_unique<operators::ProjectNode>(
      std::move(inner_proj), std::vector{col("a"), col("b")}
   );
   auto fetch =
      std::make_unique<operators::FetchNode>(std::move(outer_proj), std::nullopt, std::nullopt);

   ColumnNarrowingPass pass({col("a"), col("b")});
   pass(*fetch);

   EXPECT_NE(dynamic_cast<operators::TableScanNode*>(fetch->child.get()), nullptr);
   EXPECT_THAT(scanSchema(leafScan(*fetch)), ::testing::ElementsAre(col("a"), col("b")));
}

// --- MapNode (ZstdDecompressScalar) -> TableScanNode ---
//
// A MapNode wrapping a TableScanNode is what wrapWithDecompressIfNeeded produces.
// The decompression assignment maps a sequence-typed column (in the scan) to a
// STRING-typed output column with the same name.  The narrowing pass must:
//   1. Keep only the assignments that produce required output columns.
//   2. Keep the corresponding compressed input columns in the child scan.
//   3. Remove assignments for columns that are not required by the parent.
//   4. Eliminate the MapNode entirely when all assignments are pruned.

TEST(ColumnNarrowingPassMap, keepsSequenceColumnInChildWhenRequired) {
   auto nuc_col = colWithType("nuc", ColumnType::NUCLEOTIDE_SEQUENCE);
   auto id_col = col("id");
   auto scan = makeScan({nuc_col, id_col});
   operators::QueryNodePtr node =
      std::make_unique<operators::MapNode>(std::move(scan), decompressAssignments(nuc_col));

   // The parent sees "nuc" as STRING (MapNode output schema).
   ColumnNarrowingPass pass({col("nuc")});
   if (auto new_node = operators::visit(*node, pass)) {
      node = std::move(new_node);
   }

   // The child scan must still carry the NUCLEOTIDE_SEQUENCE column so that
   // the decompression assignment can reference it at execution time.
   EXPECT_THAT(scanSchema(leafScan(*node)), ::testing::Contains(nuc_col));
}

TEST(ColumnNarrowingPassMap, doesNotEliminateNodeWhenSequenceColumnRequired) {
   auto nuc_col = colWithType("nuc", ColumnType::NUCLEOTIDE_SEQUENCE);
   auto scan = makeScan({nuc_col});
   operators::QueryNodePtr node =
      std::make_unique<operators::MapNode>(std::move(scan), decompressAssignments(nuc_col));

   ColumnNarrowingPass pass({col("nuc")});
   if (auto new_node = operators::visit(*node, pass)) {
      node = std::move(new_node);
   }

   // The MapNode must not be removed when the decompressed column is required.
   EXPECT_NE(dynamic_cast<operators::MapNode*>(node.get()), nullptr);
}

TEST(ColumnNarrowingPassMap, prunesUnrequiredSequenceColumns) {
   auto nuc_col = colWithType("nuc", ColumnType::NUCLEOTIDE_SEQUENCE);
   auto aa_col = colWithType("aa", ColumnType::AMINO_ACID_SEQUENCE);
   auto id_col = col("id");
   auto scan = makeScan({nuc_col, aa_col, id_col});
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(decompressAssignment(nuc_col));
   assignments.push_back(decompressAssignment(aa_col));
   operators::QueryNodePtr node =
      std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   // Only "nuc" is required; "aa" and "id" should be dropped from the child scan.
   ColumnNarrowingPass pass({col("nuc")});
   if (auto new_node = operators::visit(*node, pass)) {
      node = std::move(new_node);
   }

   EXPECT_THAT(scanSchema(leafScan(*node)), ::testing::ElementsAre(nuc_col));
}

TEST(ColumnNarrowingPassMap, eliminatesNodeWhenNoAssignmentIsRequired) {
   auto nuc_col = colWithType("nuc", ColumnType::NUCLEOTIDE_SEQUENCE);
   auto id_col = col("id");
   auto scan = makeScan({nuc_col, id_col});
   operators::QueryNodePtr node =
      std::make_unique<operators::MapNode>(std::move(scan), decompressAssignments(nuc_col));

   // Only "id" is required - the decompression assignment for "nuc" can be dropped.
   ColumnNarrowingPass pass({id_col});
   if (auto new_node = operators::visit(*node, pass)) {
      node = std::move(new_node);
   }

   // The MapNode should be replaced by its child (TableScanNode).
   EXPECT_NE(dynamic_cast<operators::TableScanNode*>(node.get()), nullptr);
   EXPECT_THAT(
      dynamic_cast<operators::TableScanNode*>(node.get())->fields, ::testing::ElementsAre(id_col)
   );
}

// Surviving assignments must keep their original relative order, even when the required
// columns are requested in a different order. getOutputSchema appends newly-added columns
// (not present in the child) in assignment-vector order, so reordering would change the
// projection's output column order.
TEST(ColumnNarrowingPassMap, preservesAssignmentOrderForAddedColumns) {
   auto id_col = col("id");
   auto scan = makeScan({id_col});

   // Two assignments add new columns "x" then "y" (neither is in the child schema).
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = col("x"),
       .expression = std::make_unique<rhydb::query_engine::scalar_expressions::Int64Literal>(1),}
   );
   assignments.push_back(
      {.output_column = col("y"),
       .expression = std::make_unique<rhydb::query_engine::scalar_expressions::Int64Literal>(2),}
   );
   operators::QueryNodePtr node =
      std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   // Required columns are listed in the opposite order ("y" before "x").
   ColumnNarrowingPass pass({col("y"), col("x")});
   if (auto new_node = operators::visit(*node, pass)) {
      node = std::move(new_node);
   }

   // Both assignments survive in their original order, so getOutputSchema appends x then y.
   auto* map = dynamic_cast<operators::MapNode*>(node.get());
   ASSERT_NE(map, nullptr);
   ASSERT_EQ(map->assignments.size(), 2);
   EXPECT_EQ(map->assignments[0].output_column.name, "x");
   EXPECT_EQ(map->assignments[1].output_column.name, "y");
}

// --- FetchNode -> TableScanNode ---

TEST(ColumnNarrowingPassFetch, propagatesRequiredThroughFetch) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   auto fetch = std::make_unique<operators::FetchNode>(std::move(scan), std::nullopt, std::nullopt);

   ColumnNarrowingPass pass({col("b")});
   pass(*fetch);

   EXPECT_THAT(scanSchema(leafScan(*fetch)), ::testing::ElementsAre(col("b")));
}

// --- FilterNode -> TableScanNode ---
TEST(ColumnNarrowingPassFilter, propagatesRequiredThroughFilter) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   auto filter = std::make_unique<operators::FilterNode>(
      std::move(scan), std::make_unique<rhydb::query_engine::scalar_expressions::BoolLiteral>(true)
   );

   ColumnNarrowingPass pass({col("b")});
   pass(*filter);

   EXPECT_THAT(scanSchema(leafScan(*filter)), ::testing::ElementsAre(col("b")));
}

// --- MapNode -> TableScanNode ---

// A map produces "x := b". Only "x" is required from above, so the child does not need to provide
// "x" (the map produces it), but it must provide "b" (referenced by the assignment). "a" and "c"
// are pruned from the scan.
TEST(ColumnNarrowingPassMap, keepsReferencedColumnAndPrunesOthers) {
   auto scan = makeScan({col("a"), col("b"), col("c")});
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = col("x"),
       .expression = std::make_unique<rhydb::query_engine::scalar_expressions::FieldRef>(col("b")),}
   );
   auto map = std::make_unique<operators::MapNode>(std::move(scan), std::move(assignments));

   ColumnNarrowingPass pass({col("x")});
   pass(*map);

   EXPECT_THAT(scanSchema(leafScan(*map)), ::testing::ElementsAre(col("b")));
}

// --- UnionAllNode -> TableScanNode (both branches) ---

// UnionAll narrows each branch independently using the same required set. A custom override is
// kept (instead of the stateless base default) because `required` is mutated while walking, so
// each branch needs its own pass instance.
TEST(ColumnNarrowingPassUnionAll, narrowsBothBranchesIndependently) {
   auto left = makeScan({col("a"), col("b"), col("c")});
   auto right = makeScan({col("a"), col("b"), col("c")});
   const auto union_all =
      std::make_unique<operators::UnionAllNode>(std::move(left), std::move(right));

   ColumnNarrowingPass pass({col("a")});
   pass(*union_all);

   EXPECT_THAT(scanSchema(leafScan(*union_all->left)), ::testing::ElementsAre(col("a")));
   EXPECT_THAT(scanSchema(leafScan(*union_all->right)), ::testing::ElementsAre(col("a")));
}

}  // namespace
