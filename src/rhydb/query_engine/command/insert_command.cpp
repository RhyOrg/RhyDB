#include "rhydb/query_engine/command/insert_command.h"

#include <cstddef>
#include <set>
#include <utility>

#include "rhydb/database.h"
#include "rhydb/query_engine/command/table_insert_sink.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/optimizer/pipeline_pass_base.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::command {

namespace {

class ScannedTableCollector : public optimizer::PipelinePassBase<ScannedTableCollector> {
  public:
   using PipelinePassBase<ScannedTableCollector>::operator();

   std::set<schema::TableName> scanned_tables;

   operators::QueryNodePtr operator()(operators::TableScanNode& node) {
      scanned_tables.insert(node.table->table_name);
      return nullptr;
   }

   void collectFrom(operators::QueryNodePtr& node) { propagateToNode(node); }
};

}  // namespace

InsertCommand::InsertCommand(operators::QueryNodePtr source_query, schema::TableName target_table)
    : source_query_(std::move(source_query)),
      target_table_(std::move(target_table)) {}

arrow::Result<std::shared_ptr<arrow::Table>> InsertCommand::execute(
   Database& database,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   // Inserting streams into the target table while the source query is still producing, so the
   // source must not read the table that is being written
   ScannedTableCollector scanned_tables;
   scanned_tables.collectFrom(source_query_);
   CHECK_RHYDB_QUERY(
      !scanned_tables.scanned_tables.contains(target_table_),
      "insertInto() cannot write into table '{}' while the query reads from it",
      target_table_.getName()
   );

   auto query_plan =
      Planner::planQuery(std::move(source_query_), database.tables, query_options, request_id);

   auto target_table = database.tables.at(target_table_);
   const size_t rows_before = target_table->row_layout.numRows();

   TableInsertSink output_sink{target_table, query_plan.results_schema};
   constexpr uint64_t DEFAULT_TIMEOUT_SECONDS = 120;
   query_plan.executeAndWrite(output_sink, DEFAULT_TIMEOUT_SECONDS);

   const size_t rows_after = target_table->row_layout.numRows();
   database.updateDataVersion();

   return makeWriteSummary(
      "insertedRows", arrow::MakeScalar(static_cast<int64_t>(rows_after - rows_before))
   );
}

}  // namespace rhydb::query_engine::command
