#include "rhydb/query_engine/optimizer/node_resolution_pass.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/operators/unresolved_insertions_node.h"
#include "rhydb/query_engine/operators/unresolved_most_recent_common_ancestor_node.h"
#include "rhydb/query_engine/operators/unresolved_mutations_node.h"
#include "rhydb/query_engine/operators/unresolved_phylo_subtree_node.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::AminoAcid;
using rhydb::Nucleotide;
using rhydb::query_engine::IllegalQueryException;
using rhydb::query_engine::optimizer::NodeResolutionPass;
namespace operators = rhydb::query_engine::operators;

namespace {

std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>> makeTablesWithDefault() {
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)}
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>> tables;
   tables[rhydb::schema::TableName::getDefault()] =
      std::make_shared<rhydb::storage::Table>(rhydb::schema::TableName::getDefault(), schema);
   return tables;
}

operators::QueryNodePtr makeTableScan() {
   auto tables = makeTablesWithDefault();
   return std::make_unique<operators::TableScanNode>(
      tables.at(rhydb::schema::TableName{"default"}),
      std::make_unique<rhydb::query_engine::scalar_expressions::BoolLiteral>(true),
      std::vector<rhydb::schema::ColumnIdentifier>{}
   );
}

operators::QueryNodePtr makeNonScanChild() {
   // A FilterNode is not a TableScanNode — used to exercise the "must be a table scan" error path.
   return std::make_unique<operators::FilterNode>(
      makeTableScan(), std::make_unique<rhydb::query_engine::scalar_expressions::BoolLiteral>(true)
   );
}

std::vector<operators::MapNode::Assignment> makeMapAssignments() {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = "x", .type = rhydb::schema::ColumnType::INT64},
       .expression = std::make_unique<rhydb::query_engine::scalar_expressions::Int64Literal>(3)}
   );
   return assignments;
}

// --- mutations() ---

TEST(NodeResolutionPassMutations, onNonScanNodeThrows) {
   auto node = std::make_unique<operators::UnresolvedMutationsNode<Nucleotide>>(
      makeNonScanChild(), std::vector<std::string>{}, 0.0, std::vector<std::string>{}
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("mutations() must be applied to a table scan")
      )
   );
}

TEST(NodeResolutionPassMutations, invalidAASequenceNameThrows) {
   std::vector<std::string> seq_names{"noseq"};
   auto node = std::make_unique<operators::UnresolvedMutationsNode<AminoAcid>>(
      makeTableScan(), std::move(seq_names), 0.0, std::vector<std::string>{}
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("The database does not contain the AminoAcid sequence 'noseq'")
      )
   );
}

TEST(NodeResolutionPassMutations, invalidFieldThrows) {
   std::vector<std::string> bad_fields{"badfield"};
   auto node = std::make_unique<operators::UnresolvedMutationsNode<Nucleotide>>(
      makeTableScan(), std::vector<std::string>{}, 0.0, std::move(bad_fields)
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("The attribute 'fields' contains an invalid field 'badfield'")
      )
   );
}

// --- insertions() ---

TEST(NodeResolutionPassInsertions, onNonScanNodeThrows) {
   auto node = std::make_unique<operators::UnresolvedInsertionsNode<Nucleotide>>(
      makeNonScanChild(), std::vector<std::string>{}
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("insertions() must be applied to a table scan")
      )
   );
}

TEST(NodeResolutionPassInsertions, invalidAASequenceNameThrows) {
   std::vector<std::string> seq_names{"noseq"};
   auto node = std::make_unique<operators::UnresolvedInsertionsNode<AminoAcid>>(
      makeTableScan(), std::move(seq_names)
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("The database does not contain the AminoAcid sequence 'noseq'")
      )
   );
}

// --- phyloSubtree() ---

TEST(NodeResolutionPassPhyloSubtree, onNonScanNodeThrows) {
   auto node = std::make_unique<operators::UnresolvedPhyloSubtreeNode>(
      makeNonScanChild(), std::string{"col"}, false, false
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("phyloSubtree() must be applied to a table scan")
      )
   );
}

// --- mostRecentCommonAncestor() ---

TEST(NodeResolutionPassMostRecentCommonAncestor, onNonScanNodeThrows) {
   auto node = std::make_unique<operators::UnresolvedMostRecentCommonAncestorNode>(
      makeNonScanChild(), std::string{"col"}, false
   );
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(node)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("mostRecentCommonAncestor() must be applied to a table scan")
      )
   );
}

// --- map() ---

TEST(NodeResolutionPassMap, resolvesUnresolvedChildBelowMap) {
   auto unresolved = std::make_unique<operators::UnresolvedInsertionsNode<Nucleotide>>(
      makeTableScan(), std::vector<std::string>{}
   );
   auto map = std::make_unique<operators::MapNode>(std::move(unresolved), makeMapAssignments());

   auto result = NodeResolutionPass::run(std::move(map));

   // The MapNode is retained; its unresolved child was resolved into a concrete node.
   ASSERT_EQ(result->kind(), operators::NodeKind::MAP);
   auto* resolved_map = dynamic_cast<operators::MapNode*>(result.get());
   EXPECT_EQ(resolved_map->child->kind(), operators::NodeKind::INSERTIONS_NUCLEOTIDE);
}

TEST(NodeResolutionPassMap, propagatesChildErrorThroughMap) {
   auto unresolved = std::make_unique<operators::UnresolvedInsertionsNode<Nucleotide>>(
      makeNonScanChild(), std::vector<std::string>{}
   );
   auto map = std::make_unique<operators::MapNode>(std::move(unresolved), makeMapAssignments());
   EXPECT_THAT(
      [&]() { (void)NodeResolutionPass::run(std::move(map)); },
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("insertions() must be applied to a table scan")
      )
   );
}

// --- happy-path resolutions (the replacement-returning handlers) ---
TEST(NodeResolutionPassMutations, resolvesToMutationsNode) {
   auto node = std::make_unique<operators::UnresolvedMutationsNode<Nucleotide>>(
      makeTableScan(), std::vector<std::string>{}, 0.0, std::vector<std::string>{}
   );

   auto result = NodeResolutionPass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::MUTATIONS_NUCLEOTIDE);
}

TEST(NodeResolutionPassInsertions, resolvesToInsertionsNode) {
   auto node = std::make_unique<operators::UnresolvedInsertionsNode<Nucleotide>>(
      makeTableScan(), std::vector<std::string>{}
   );

   auto result = NodeResolutionPass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::INSERTIONS_NUCLEOTIDE);
}

}  // namespace
