#include "rhydb/query_engine/scalar_expressions/symbol_equals.h"

#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/query_engine/query_parse_sequence_name.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
SymbolOrDot<SymbolType>::SymbolOrDot(SymbolType::Symbol symbol)
    : value(symbol) {}

template <typename SymbolType>
SymbolOrDot<SymbolType> SymbolOrDot<SymbolType>::dot() {
   return SymbolOrDot<SymbolType>();
}

template <typename SymbolType>
char SymbolOrDot<SymbolType>::asChar() const {
   return value.has_value() ? SymbolType::symbolToChar(value.value()) : '.';
}

template <typename SymbolType>
SymbolType::Symbol SymbolOrDot<SymbolType>::getSymbolOrReplaceDotWith(
   SymbolType::Symbol replace_dot_with
) const {
   return value.value_or(replace_dot_with);
}

template class SymbolOrDot<AminoAcid>;
template class SymbolOrDot<Nucleotide>;

template <typename SymbolType>
SymbolEquals<SymbolType>::SymbolEquals(
   schema::ColumnIdentifier column,
   uint32_t position_idx,
   SymbolOrDot<SymbolType> value
)
    : column(std::move(column)),
      position_idx(position_idx),
      value(value) {}

template <typename SymbolType>
std::string SymbolEquals<SymbolType>::toString() const {
   return fmt::format("{}:{}{}", column.name, position_idx + 1, value.asChar());
}

template <typename SymbolType>
std::vector<schema::ColumnIdentifier> SymbolEquals<SymbolType>::freeIUs() const {
   return {column};
}

template <typename SymbolType>
std::unique_ptr<ScalarExpression> SymbolEquals<SymbolType>::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   const auto valid_sequence_name = validateSequenceName<SymbolType>(column.name, *table.schema);

   const auto& sequence_column = table.getColumn<typename SymbolType::Column>(valid_sequence_name);

   CHECK_RHYDB_QUERY(
      position_idx < sequence_column.metadata->reference_sequence.size(),
      "{} position is out of bounds {} > {}",
      getFilterName(),
      position_idx + 1,
      sequence_column.metadata->reference_sequence.size()
   );

   auto symbol =
      value.getSymbolOrReplaceDotWith(sequence_column.metadata->reference_sequence.at(position_idx)
      );
   if (mode == UPPER_BOUND) {
      auto symbols_to_match = SymbolType::AMBIGUITY_SYMBOLS.at(symbol);
      return std::make_unique<SymbolInSet<SymbolType>>(column, position_idx, symbols_to_match);
   }

   return std::make_unique<SymbolInSet<SymbolType>>(
      column, position_idx, std::vector<typename SymbolType::Symbol>{symbol}
   );
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> SymbolEquals<SymbolType>::compile(
   const storage::Table& /*table*/
) const {
   throw QueryCompilationException("SymbolEquals should have been rewritten before compilation");
}

template class SymbolEquals<AminoAcid>;
template class SymbolEquals<Nucleotide>;

}  // namespace rhydb::query_engine::scalar_expressions
