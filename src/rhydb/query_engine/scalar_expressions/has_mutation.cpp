#include "rhydb/query_engine/scalar_expressions/has_mutation.h"

#include <memory>
#include <utility>
#include <vector>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/query_engine/query_parse_sequence_name.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
HasMutation<SymbolType>::HasMutation(schema::ColumnIdentifier column, uint32_t position_idx)
    : column(std::move(column)),
      position_idx(position_idx) {}

template <typename SymbolType>
std::string HasMutation<SymbolType>::toString() const {
   return column.name + ":" + std::to_string(position_idx + 1);
}

template <typename SymbolType>
std::vector<schema::ColumnIdentifier> HasMutation<SymbolType>::freeIUs() const {
   return {column};
}

template <typename SymbolType>
std::unique_ptr<ScalarExpression> HasMutation<SymbolType>::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   const auto valid_sequence_name = validateSequenceName<SymbolType>(column.name, *table.schema);

   const auto& sequence_column =
      table.columns.getColumns<typename SymbolType::Column>().at(valid_sequence_name);

   auto column_metadata =
      table.schema->getColumnMetadata<typename SymbolType::Column>(valid_sequence_name).value();
   CHECK_RHYDB_QUERY(
      position_idx < column_metadata->reference_sequence.size(),
      "Has{}Mutation position is out of bounds {} > {}",
      SymbolType::SYMBOL_NAME,
      position_idx + 1,
      sequence_column.metadata->reference_sequence.size()
   )

   auto ref_symbol = sequence_column.metadata->reference_sequence.at(position_idx);

   std::vector<typename SymbolType::Symbol> symbols =
      std::vector(SymbolType::SYMBOLS.begin(), SymbolType::SYMBOLS.end());
   if (mode == AmbiguityMode::UPPER_BOUND) {
      // We can only be sure, that the symbol did not mutate, if the ref_symbol is at that position
      std::erase(symbols, ref_symbol);
   } else {
      // Remove all symbols that could match the searched base
      for (const auto symbol : SymbolType::AMBIGUITY_SYMBOLS.at(ref_symbol)) {
         std::erase(symbols, symbol);
      }
   }
   return std::make_unique<SymbolInSet<SymbolType>>(column, position_idx, std::move(symbols));
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> HasMutation<SymbolType>::compile(
   const storage::Table& /*table*/
) const {
   throw QueryCompilationException{
      "Has{}Mutation expression must be eliminated in query rewrite phase", SymbolType::SYMBOL_NAME,
   };
}

template class HasMutation<AminoAcid>;
template class HasMutation<Nucleotide>;

}  // namespace rhydb::query_engine::scalar_expressions
