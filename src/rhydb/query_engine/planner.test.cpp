#include "rhydb/query_engine/planner.h"

#include <map>
#include <memory>
#include <stdexcept>

#include <arrow/acero/exec_plan.h>
#include <arrow/result.h>
#include <arrow/status.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::query_engine::Planner;
namespace operators = rhydb::query_engine::operators;

namespace {

class ErrorQueryNode final : public operators::QueryNode {
  public:
   [[nodiscard]] std::vector<rhydb::schema::ColumnIdentifier> getOutputSchema() const override {
      return {};
   }

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& /*plan*/,
      const std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>>& /*tables*/,
      const rhydb::config::QueryOptions& /*query_options*/
   ) const override {
      return arrow::Status::ExecutionError("induced test error");
   }

   [[nodiscard]] operators::NodeKind kind() const override {
      return operators::NodeKind::COUNT_FILTER;
   }

   [[nodiscard]] nlohmann::json toJson() const override { return {{"type", "ErrorQueryNode"}}; }
};

TEST(PlannerPlanQuery, arrowErrorThrows) {
   rhydb::config::QueryOptions options{.materialization_cutoff = 1024};
   auto node = std::make_unique<ErrorQueryNode>();
   EXPECT_THAT(
      [&]() { (void)Planner::planQuery(std::move(node), {}, options, "test"); },
      ThrowsMessage<std::runtime_error>(::testing::HasSubstr("Error when planning query execution"))
   );
}

std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>>
tablesWithSequenceColumnFirst() {
   using rhydb::AminoAcid;
   using rhydb::schema::ColumnIdentifier;
   using rhydb::schema::ColumnType;
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::SequenceColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   const ColumnIdentifier sequence{.name = "E", .type = ColumnType::AMINO_ACID_SEQUENCE};
   const ColumnIdentifier primary_key{.name = "id", .type = ColumnType::STRING};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {sequence,
       std::make_shared<SequenceColumnMetadata<AminoAcid>>(
          sequence.name, std::vector<AminoAcid::Symbol>{AminoAcid::Symbol::A}
       )},
      {primary_key, std::make_shared<StringColumnMetadata>(primary_key.name)},
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), primary_key);
   std::map<rhydb::schema::TableName, std::shared_ptr<rhydb::storage::Table>> tables;
   tables[rhydb::schema::TableName::getDefault()] =
      std::make_shared<rhydb::storage::Table>(rhydb::schema::TableName::getDefault(), schema);
   return tables;
}

// A bare count(*) over a table whose first column is a sequence must still optimize to a
// CountFilterNode (reading the filter's cardinality), not stay an aggregate over the decompress
// map. Regression test for #1568.
TEST(PlannerOptimize, bareCountOverSequenceFirstTableBecomesCountFilter) {
   auto tables = tablesWithSequenceColumnFirst();
   auto node = rhydb::query_engine::saneql::parseAndConvertToQueryTree(
      "default.groupBy({n := count()})", tables
   );

   auto optimized = Planner::optimize(std::move(node), "test");

   ASSERT_EQ(optimized->kind(), operators::NodeKind::COUNT_FILTER);
}

}  // namespace
