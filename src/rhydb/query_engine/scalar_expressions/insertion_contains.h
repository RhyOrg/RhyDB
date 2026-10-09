#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <type_traits>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb {
class Nucleotide;
}

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
class InsertionContains : public ScalarExpression {
  private:
   schema::ColumnIdentifier column;
   uint32_t position_idx;
   std::string value;

  public:
   explicit InsertionContains(
      schema::ColumnIdentifier column,
      uint32_t position_idx,
      std::string value
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<InsertionContains>(column, position_idx, value);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = std::is_same_v<SymbolType, Nucleotide>
                                   ? Kind::INSERTION_CONTAINS_NUCLEOTIDE
                                   : Kind::INSERTION_CONTAINS_AMINO_ACID;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
