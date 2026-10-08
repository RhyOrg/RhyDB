#include "rhydb/query_engine/scalar_expressions/insertion_contains.h"

#include <utility>
#include <vector>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/filter/operators/bitmap_producer.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_parse_sequence_name.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/insertion_format_exception.h"

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
InsertionContains<SymbolType>::InsertionContains(
   schema::ColumnIdentifier column,
   uint32_t position_idx,
   std::string value
)
    : column(std::move(column)),
      position_idx(position_idx),
      value(std::move(value)) {}

template <typename SymbolType>
std::string InsertionContains<SymbolType>::toString() const {
   const std::string sequence_string = "The sequence '" + column.name + "'";

   return sequence_string + " has insertion '" + value + "'";
}

template <typename SymbolType>
std::vector<schema::ColumnIdentifier> InsertionContains<SymbolType>::freeIUs() const {
   return {column};
}

template <typename SymbolType>
std::unique_ptr<ScalarExpression> InsertionContains<SymbolType>::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<InsertionContains<SymbolType>>(column, position_idx, value);
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> InsertionContains<SymbolType>::compile(
   const storage::Table& table
) const {
   const auto valid_sequence_name = validateSequenceName<SymbolType>(column.name, *table.schema);

   const auto& sequence_store = table.getColumn<typename SymbolType::Column>(valid_sequence_name);
   const size_t reference_sequence_size = sequence_store.metadata->reference_sequence.size();
   CHECK_RHYDB_QUERY(
      position_idx <= reference_sequence_size,
      "the requested insertion position ({}) is larger than the length of the reference sequence "
      "({}) for sequence '{}'",
      position_idx,
      reference_sequence_size,
      valid_sequence_name
   );
   return std::make_unique<filter::operators::BitmapProducer>(
      [&sequence_store, position_idx = position_idx, value = value] {
         try {
            auto search_result = sequence_store.insertion_index.search(position_idx, value);
            return Bitmap(std::move(*search_result));
         } catch (const storage::InsertionFormatException& exception) {
            throw IllegalQueryException(
               "The field 'value' in the InsertionContains expression does not contain a valid "
               "regex "
               "pattern: \"{}\". It must only consist of {} symbols and the regex symbol '.*'. "
               "Also note "
               "that the stop codon * must be escaped correctly with a \\ in amino acid queries.",
               value,
               SymbolType::SYMBOL_NAME_LOWER_CASE
            );
         }
      },
      table.row_layout
   );
}

template class InsertionContains<Nucleotide>;
template class InsertionContains<AminoAcid>;

}  // namespace rhydb::query_engine::scalar_expressions
