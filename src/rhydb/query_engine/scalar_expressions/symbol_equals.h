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
class SymbolOrDot {
   std::optional<typename SymbolType::Symbol> value;

   SymbolOrDot() = default;

  public:
   static SymbolOrDot<SymbolType> dot();

   explicit SymbolOrDot(SymbolType::Symbol symbol);

   [[nodiscard]] SymbolType::Symbol getSymbolOrReplaceDotWith(SymbolType::Symbol replace_dot_with
   ) const;

   [[nodiscard]] char asChar() const;
};

template <typename SymbolType>
class SymbolEquals : public ScalarExpression {
   schema::ColumnIdentifier column;
   uint32_t position_idx;
   SymbolOrDot<SymbolType> value;

  public:
   explicit SymbolEquals(
      schema::ColumnIdentifier column,
      uint32_t position_idx,
      SymbolOrDot<SymbolType> value
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<SymbolEquals>(column, position_idx, value);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = std::is_same_v<SymbolType, Nucleotide>
                                   ? Kind::SYMBOL_EQUALS_NUCLEOTIDE
                                   : Kind::SYMBOL_EQUALS_AMINO_ACID;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(const storage::Table& table
   ) const override;

  private:
   static std::string getFilterName() {
      return fmt::format("SymbolEquals<{}>", SymbolType::SYMBOL_NAME);
   }
};

}  // namespace rhydb::query_engine::scalar_expressions
