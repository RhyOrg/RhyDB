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
#include "rhydb/storage/column/row_layout.h"
#include "rhydb/storage/column/sequence_column.h"

namespace rhydb {
class Nucleotide;
}

namespace rhydb::query_engine::scalar_expressions {

class Or;

/// Compile a "symbol at `position_idx` is in `symbols`" filter directly against a sequence column,
/// without requiring a surrounding table or schema. Reference, missing (no coverage) and the
/// remaining symbol handling are resolved exactly as in the regular query engine. Exposed so that
/// callers holding only a column (e.g. the bitmap aggregation node's per-position grouping) can
/// reuse this machinery.
template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> compileSymbolInSet(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   uint32_t position_idx,
   const std::vector<typename SymbolType::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
);

template <typename SymbolType>
class SymbolInSet : public ScalarExpression {
   friend class Or;  // For optimization under Or

   schema::ColumnIdentifier column;
   uint32_t position_idx;
   std::vector<typename SymbolType::Symbol> symbols;

  public:
   explicit SymbolInSet(
      schema::ColumnIdentifier column,
      uint32_t position_idx,
      std::vector<typename SymbolType::Symbol> symbols
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<SymbolInSet>(column, position_idx, symbols);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = std::is_same_v<SymbolType, Nucleotide>
                                   ? Kind::SYMBOL_IN_SET_NUCLEOTIDE
                                   : Kind::SYMBOL_IN_SET_AMINO_ACID;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;

  private:
   static std::string getFilterName() {
      return fmt::format("SymbolInSet<{}>", SymbolType::SYMBOL_NAME);
   }
};

}  // namespace rhydb::query_engine::scalar_expressions
