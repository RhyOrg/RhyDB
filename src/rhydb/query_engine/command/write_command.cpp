#include "rhydb/query_engine/command/write_command.h"

#include <utility>
#include <variant>

#include <arrow/array/util.h>
#include <arrow/compute/exec.h>
#include <arrow/table.h>

#include "rhydb/query_engine/command/create_table_command.h"
#include "rhydb/query_engine/command/delete_command.h"
#include "rhydb/query_engine/command/insert_command.h"
#include "rhydb/query_engine/command/update_command.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/query_engine/saneql/parser.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"

namespace rhydb::query_engine::command {

using saneql::BoundArguments;
using saneql::FunctionSignature;
using saneql::ParameterDefinition;
using saneql::Tables;

arrow::Result<std::shared_ptr<arrow::Table>> makeWriteSummary(
   const std::string& name,
   const std::shared_ptr<arrow::Scalar>& value
) {
   ARROW_ASSIGN_OR_RAISE(auto array, arrow::MakeArrayFromScalar(*value, 1));
   return arrow::Table::Make(arrow::schema({arrow::field(name, value->type)}), {std::move(array)});
}

arrow::Status writeToSink(
   const arrow::Table& write_result,
   exec_node::ArrowBatchSink& output_sink
) {
   arrow::TableBatchReader reader{write_result};
   std::shared_ptr<arrow::RecordBatch> batch;
   while (true) {
      ARROW_RETURN_NOT_OK(reader.ReadNext(&batch));
      if (batch == nullptr) {
         break;
      }
      ARROW_RETURN_NOT_OK(output_sink.writeBatch(arrow::compute::ExecBatch{*batch}));
   }
   return output_sink.finish();
}

std::unique_ptr<scalar_expressions::ScalarExpression> buildWherePredicate(
   const BoundArguments& args,
   const std::shared_ptr<storage::Table>& table,
   const Tables& tables
) {
   const auto* where = args.get("where");
   if (where == nullptr) {
      return std::make_unique<scalar_expressions::BoolLiteral>(true);
   }
   const auto columns =
      saneql::makeTableScan(table, std::make_unique<scalar_expressions::BoolLiteral>(true))
         ->getOutputSchema();
   return saneql::convertToFilter(*where, columns, tables);
}

namespace {

WriteCommandPtr buildInsertInto(const BoundArguments& args, const Tables& tables) {
   const std::string target_name = saneql::ast::extractIdentifierName(args.at("target"));
   auto target_table = schema::TableName(target_name);
   CHECK_RHYDB_QUERY(
      tables.contains(target_table),
      "insertInto() target table '{}' not found in database",
      target_name
   );
   auto source_query = saneql::convertExpression(args.at("input"), tables);
   return std::make_unique<InsertCommand>(std::move(source_query), std::move(target_table));
}

}  // namespace

WriteStatementRegistry::WriteStatementRegistry() {
   registerStatement(
      "insertInto",
      FunctionSignature{
         {ParameterDefinition{.name = "input"}, ParameterDefinition{.name = "target"}},
      },
      buildInsertInto
   );
   registerStatement(
      "createTable",
      FunctionSignature{
         {
            ParameterDefinition{.name = "table"},
            ParameterDefinition{.name = "columns"},
            ParameterDefinition{.name = "primaryKey", .required = false, .positional = false},
         },
      },
      buildCreateTable
   );
   registerStatement(
      "update",
      FunctionSignature{
         {
            ParameterDefinition{.name = "table"},
            ParameterDefinition{.name = "set"},
            ParameterDefinition{.name = "where", .required = false, .positional = false},
         },
      },
      buildUpdate
   );
   registerStatement(
      "delete",
      FunctionSignature{
         {
            ParameterDefinition{.name = "table"},
            ParameterDefinition{.name = "where", .required = false, .positional = false},
         },
      },
      buildDelete
   );
}

void WriteStatementRegistry::registerStatement(
   std::string name,
   FunctionSignature signature,
   WriteStatementHandler handler
) {
   entries_[std::move(name)] =
      Entry{.signature = std::move(signature), .handler = std::move(handler)};
}

const WriteStatementRegistry::Entry* WriteStatementRegistry::findStatement(
   const std::string& name
) const {
   auto it = entries_.find(name);
   if (it == entries_.end()) {
      return nullptr;
   }
   return &it->second;
}

WriteStatementRegistry& WriteStatementRegistry::instance() {
   static WriteStatementRegistry registry;
   return registry;
}

Request parseRequest(std::string_view query_string, const Tables& tables) {
   saneql::Parser parser(query_string);
   auto ast = parser.parse();

   // A write statement is a root function call whose name is registered as a write statement;
   // anything else is an ordinary read query and goes through the unchanged read conversion path.
   if (std::holds_alternative<saneql::ast::FunctionCall>(ast->value)) {
      const auto& call = std::get<saneql::ast::FunctionCall>(ast->value);
      if (
         const auto* entry = WriteStatementRegistry::instance().findStatement(call.function_name)
      ) {
         auto bound = saneql::bindArguments(
            call.function_name, entry->signature, call.positional_arguments, call.named_arguments
         );
         return entry->handler(bound, tables);
      }
   }

   return saneql::convertToQueryTree(*ast, tables);
}

}  // namespace rhydb::query_engine::command
