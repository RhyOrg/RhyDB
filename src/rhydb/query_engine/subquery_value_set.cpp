#include "rhydb/query_engine/subquery_value_set.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/acero/options.h>
#include <arrow/array.h>
#include <arrow/array/array_binary.h>
#include <arrow/array/array_primitive.h>

#include "rhydb/config/runtime_config.h"
#include "rhydb/query_engine/exec_node/arrow_batch_sink.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"

namespace rhydb::query_engine {

namespace {

namespace se = scalar_expressions;
using ScalarExpressionPtr = std::unique_ptr<se::ScalarExpression>;

/// Turns the values of a subquery's single output column into typed scalar literals. Each supported
/// Arrow array type maps to the matching RhyDB literal, so `in(<subquery>)` stays type-agnostic.
class LiteralCollectingVisitor : public arrow::ArrayVisitor {
   std::vector<ScalarExpressionPtr>& literals;

   template <typename LiteralType, typename ValueType>
   void add(ValueType&& value) {
      literals.push_back(std::make_unique<LiteralType>(std::forward<ValueType>(value)));
   }

  public:
   explicit LiteralCollectingVisitor(std::vector<ScalarExpressionPtr>& literals)
       : literals(literals) {}

   arrow::Status Visit(const arrow::StringArray& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            add<se::StringLiteral>(std::string{array.GetView(i)});
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::Int32Array& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            add<se::Int64Literal>(array.Value(i));
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::Int64Array& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            add<se::Int64Literal>(array.Value(i));
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::DoubleArray& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            add<se::FloatLiteral>(array.Value(i));
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::FloatArray& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            add<se::FloatLiteral>(array.Value(i));
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::Date32Array& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            // common::Date32 is an int32 day count, which is exactly Date32Array's storage.
            add<se::DateLiteral>(array.Value(i));
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::BooleanArray& array) override {
      for (int64_t i = 0; i < array.length(); ++i) {
         if (!array.IsNull(i)) {
            add<se::BoolLiteral>(array.Value(i));
         }
      }
      return arrow::Status::OK();
   }
};

/// Collects the single output column of every batch into `visitor`.
class LiteralCollectingSink : public exec_node::ArrowBatchSink {
   LiteralCollectingVisitor& visitor;

  public:
   explicit LiteralCollectingSink(LiteralCollectingVisitor& visitor)
       : visitor(visitor) {}

   arrow::Status writeBatch(const arrow::compute::ExecBatch& batch) override {
      if (batch.values.empty()) {
         return arrow::Status::OK();
      }
      const auto& datum = batch.values.front();
      std::shared_ptr<arrow::Array> array;
      if (datum.is_array()) {
         array = datum.make_array();
      } else {
         ARROW_ASSIGN_OR_RAISE(array, arrow::MakeArrayFromScalar(*datum.scalar(), batch.length));
      }
      return array->Accept(&visitor);
   }

   arrow::Status finish() override { return arrow::Status::OK(); }
};

bool isSupportedScalarType(schema::ColumnType type) {
   switch (type) {
      case schema::ColumnType::STRING:
      case schema::ColumnType::DICTIONARY_ENCODED:
      case schema::ColumnType::INT32:
      case schema::ColumnType::INT64:
      case schema::ColumnType::FLOAT:
      case schema::ColumnType::DATE32:
      case schema::ColumnType::BOOL:
         return true;
      default:
         return false;
   }
}

}  // namespace

std::vector<ScalarExpressionPtr> materializeSubqueryColumnLiterals(
   operators::QueryNodePtr subquery,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   std::string_view request_id
) {
   const auto output_schema = subquery->getOutputSchema();
   CHECK_RHYDB_QUERY(
      output_schema.size() == 1,
      "in(<subquery>) requires the subquery to produce exactly one column, but it produces {}.",
      output_schema.size()
   );
   CHECK_RHYDB_QUERY(
      isSupportedScalarType(output_schema.front().type),
      "in(<subquery>): the subquery column has type {}, which is not a supported scalar type.",
      schema::columnTypeToString(output_schema.front().type)
   );

   auto query_plan = Planner::planQuery(
      std::move(subquery), tables, config::RuntimeConfig::withDefaults().query_options, request_id
   );

   std::vector<ScalarExpressionPtr> literals;
   LiteralCollectingVisitor visitor{literals};
   LiteralCollectingSink sink{visitor};
   constexpr uint64_t TIMEOUT_SECONDS = 120;
   query_plan.executeAndWrite(sink, TIMEOUT_SECONDS);
   return literals;
}

}  // namespace rhydb::query_engine
