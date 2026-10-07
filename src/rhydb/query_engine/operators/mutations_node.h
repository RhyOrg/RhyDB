#pragma once

#include <map>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

#include <arrow/result.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

/// Computes SymbolType mutations for matching rows.
template <typename SymbolType>
class MutationsNode final : public QueryNode {
  public:
   constexpr static std::string_view MUTATION_FROM_FIELD_NAME = "mutationFrom";
   constexpr static std::string_view MUTATION_TO_FIELD_NAME = "mutationTo";
   constexpr static std::string_view POSITION_FIELD_NAME = "position";
   constexpr static std::string_view SEQUENCE_FIELD_NAME = "sequenceName";
   constexpr static std::string_view PROPORTION_FIELD_NAME = "proportion";
   constexpr static std::string_view COVERAGE_FIELD_NAME = "coverage";
   constexpr static std::string_view COUNT_FIELD_NAME = "count";
   static constexpr std::array<std::string_view, 7> VALID_FIELDS{
      MUTATION_FROM_FIELD_NAME,
      MUTATION_TO_FIELD_NAME,
      POSITION_FIELD_NAME,
      SEQUENCE_FIELD_NAME,
      PROPORTION_FIELD_NAME,
      COVERAGE_FIELD_NAME,
      COUNT_FIELD_NAME,
   };

   std::shared_ptr<storage::Table> table;
   std::unique_ptr<scalar_expressions::ScalarExpression> filter;
   std::vector<schema::ColumnIdentifier> sequence_columns;
   double min_proportion;
   std::vector<std::string_view> fields;

   MutationsNode(
      std::shared_ptr<storage::Table> table,
      std::unique_ptr<scalar_expressions::ScalarExpression> filter,
      std::vector<schema::ColumnIdentifier> sequence_columns,
      double min_proportion,
      std::vector<std::string_view> fields
   )
       : table(std::move(table)),
         filter(std::move(filter)),
         sequence_columns(std::move(sequence_columns)),
         min_proportion(min_proportion),
         fields(std::move(fields)) {}

   [[nodiscard]] std::vector<schema::ColumnIdentifier> getOutputSchema() const override {
      using rhydb::schema::ColumnType;
      std::vector<schema::ColumnIdentifier> output_fields;
      if (std::ranges::find(fields, MUTATION_FROM_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(MUTATION_FROM_FIELD_NAME), ColumnType::STRING);
      }
      if (std::ranges::find(fields, MUTATION_TO_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(MUTATION_TO_FIELD_NAME), ColumnType::STRING);
      }
      if (std::ranges::find(fields, SEQUENCE_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(SEQUENCE_FIELD_NAME), ColumnType::STRING);
      }
      if (std::ranges::find(fields, POSITION_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(POSITION_FIELD_NAME), ColumnType::INT32);
      }
      if (std::ranges::find(fields, PROPORTION_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(PROPORTION_FIELD_NAME), ColumnType::FLOAT);
      }
      if (std::ranges::find(fields, COVERAGE_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(COVERAGE_FIELD_NAME), ColumnType::INT32);
      }
      if (std::ranges::find(fields, COUNT_FIELD_NAME) != fields.end()) {
         output_fields.emplace_back(std::string(COUNT_FIELD_NAME), ColumnType::INT32);
      }
      return output_fields;
   }

   [[nodiscard]] arrow::Result<arrow::acero::ExecNode*> addToExecPlan(
      arrow::acero::ExecPlan& plan,
      const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
      const config::QueryOptions& query_options
   ) const override;

   [[nodiscard]] NodeKind kind() const override {
      if constexpr (std::is_same_v<SymbolType, rhydb::Nucleotide>) {
         return NodeKind::MUTATIONS_NUCLEOTIDE;
      } else {
         return NodeKind::MUTATIONS_AMINO_ACID;
      }
   }

   [[nodiscard]] nlohmann::json toJson() const override;
};

}  // namespace rhydb::query_engine::operators
