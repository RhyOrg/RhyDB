#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"

#include <memory>
#include <utility>
#include <vector>

#include <fmt/ranges.h>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/intersection.h"
#include "rhydb/query_engine/filter/operators/is_in_covered_region.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/filter/operators/union.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/query_engine/query_parse_sequence_name.h"

using rhydb::storage::column::SequenceColumn;

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
SymbolInSet<SymbolType>::SymbolInSet(
   schema::ColumnIdentifier column,
   uint32_t position_idx,
   std::vector<typename SymbolType::Symbol> symbols
)
    : column(std::move(column)),
      position_idx(position_idx),
      symbols(std::move(symbols)) {}

template <typename SymbolType>
std::string SymbolInSet<SymbolType>::toString() const {
   std::string symbols_string = fmt::format(
      "{}",
      fmt::join(
         symbols | std::views::transform([](SymbolType::Symbol symbol) {
            return SymbolType::symbolToChar(symbol);
         }),
         ", "
      )
   );

   return fmt::format(
      "({}:symbol at position {} in {{{}}})", column.name, position_idx + 1, symbols_string
   );
}

template <typename SymbolType>
std::vector<schema::ColumnIdentifier> SymbolInSet<SymbolType>::freeIUs() const {
   return {column};
}

namespace {

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> negateSymbolsExcluding(
   const std::vector<typename SymbolType::Symbol>& symbols,
   typename SymbolType::Symbol excluded_symbol
) {
   std::vector<typename SymbolType::Symbol> result;
   for (const auto& symbol : SymbolType::SYMBOLS) {
      if (symbol == excluded_symbol) {
         continue;
      }
      if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) {
         result.push_back(symbol);
      }
   }
   return result;
}

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> negateSymbols(
   const std::vector<typename SymbolType::Symbol>& symbols
) {
   std::vector<typename SymbolType::Symbol> result;
   for (const auto& symbol : SymbolType::SYMBOLS) {
      if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) {
         result.push_back(symbol);
      }
   }
   return result;
}

// To make the difference, we produce `Left & !Right`
std::unique_ptr<filter::operators::Operator> makeDifference(
   std::unique_ptr<filter::operators::Operator> left,
   std::unique_ptr<filter::operators::Operator> right,
   const storage::column::RowLayout& row_layout
) {
   filter::operators::OperatorVector non_negated_operators;
   non_negated_operators.push_back(std::move(left));
   filter::operators::OperatorVector negated_operators;
   negated_operators.push_back(std::move(right));
   return std::make_unique<filter::operators::Intersection>(
      std::move(non_negated_operators), std::move(negated_operators), row_layout
   );
}

// A row without a sequence has no value at any position, so it must not match a symbol filter. It
// is stored with an empty covered region, which makes it indistinguishable from a row that is
// merely not covered at this position, i.e. it would otherwise be matched by the missing symbol.
// Only the compilations that start from the not-covered rows need this; the ones that start from
// the covered rows or from the mutation index never see a null row in the first place.
template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> excludeNullSequences(
   std::unique_ptr<filter::operators::Operator> operator_,
   const SequenceColumn<SymbolType>& sequence_column,
   const storage::column::RowLayout& row_layout
) {
   if (sequence_column.null_bitmap.isEmpty()) {
      return operator_;
   }
   return makeDifference(
      std::move(operator_),
      std::make_unique<filter::operators::IndexScan>(
         Bitmap{&sequence_column.null_bitmap}, row_layout
      ),
      row_layout
   );
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> compileWithMissingSymbolAndReference(
   const SequenceColumn<SymbolType>& sequence_column,
   uint32_t position_idx,
   const std::vector<typename SymbolType::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
) {
   // as the missing symbol and the reference symbol are included, we can just negate the other
   // symbols
   auto negated_symbols = negateSymbols<SymbolType>(symbols);
   auto bitmap =
      Bitmap::fromContainerViews(sequence_column.vertical_sequence_index.getMatchingContainerViews(
         position_idx, negated_symbols
      ));
   return excludeNullSequences(
      std::make_unique<filter::operators::Complement>(
         std::make_unique<filter::operators::IndexScan>(std::move(bitmap), row_layout), row_layout
      ),
      sequence_column,
      row_layout
   );
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> compileWithMissingSymbol(
   const SequenceColumn<SymbolType>& sequence_column,
   uint32_t position_idx,
   const std::vector<typename SymbolType::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
) {
   // The missing symbol is included, so we start with the sequences with no coverage at this
   // position and then add the sequences with the mutation symbols
   auto bitmap = Bitmap::fromContainerViews(
      sequence_column.vertical_sequence_index.getMatchingContainerViews(position_idx, symbols)
   );

   filter::operators::OperatorVector operators_for_union;
   operators_for_union.push_back(std::make_unique<filter::operators::Selection>(
      std::make_unique<filter::operators::IsInCoveredRegion>(
         &sequence_column.horizontal_coverage_index,
         position_idx,
         filter::operators::IsInCoveredRegion::Comparator::IS_NOT_COVERED
      ),
      row_layout
   ));
   operators_for_union.push_back(
      std::make_unique<filter::operators::IndexScan>(std::move(bitmap), row_layout)
   );
   return excludeNullSequences(
      std::make_unique<filter::operators::Union>(std::move(operators_for_union), row_layout),
      sequence_column,
      row_layout
   );
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> compileWithReference(
   const SequenceColumn<SymbolType>& sequence_column,
   uint32_t position_idx,
   const std::vector<typename SymbolType::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
) {
   // The reference symbol is included, so we start with the sequences with coverage at this
   // position and then remove the sequences with the negated mutation symbols
   auto negated_symbols = negateSymbolsExcluding<SymbolType>(symbols, SymbolType::SYMBOL_MISSING);
   auto bitmap =
      Bitmap::fromContainerViews(sequence_column.vertical_sequence_index.getMatchingContainerViews(
         position_idx, negated_symbols
      ));

   return makeDifference(
      std::make_unique<filter::operators::Selection>(
         std::make_unique<filter::operators::IsInCoveredRegion>(
            &sequence_column.horizontal_coverage_index,
            position_idx,
            filter::operators::IsInCoveredRegion::Comparator::IS_COVERED
         ),
         row_layout
      ),
      std::make_unique<filter::operators::IndexScan>(std::move(bitmap), row_layout),
      row_layout
   );
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> compileOnlyMutations(
   const SequenceColumn<SymbolType>& sequence_column,
   uint32_t position_idx,
   const std::vector<typename SymbolType::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
) {
   // All our results are fully included in the vertical sequence index
   auto bitmap = Bitmap::fromContainerViews(
      sequence_column.vertical_sequence_index.getMatchingContainerViews(position_idx, symbols)
   );
   return std::make_unique<filter::operators::IndexScan>(std::move(bitmap), row_layout);
}

}  // namespace

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> compileSymbolInSet(
   const SequenceColumn<SymbolType>& sequence_column,
   uint32_t position_idx,
   const std::vector<typename SymbolType::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
) {
   CHECK_RHYDB_QUERY(
      position_idx < sequence_column.metadata->reference_sequence.size(),
      "SymbolInSet<{}> position is out of bounds {} > {}",
      SymbolType::SYMBOL_NAME,
      position_idx + 1,
      sequence_column.metadata->reference_sequence.size()
   );

   auto local_reference_symbol = sequence_column.getLocalReferencePosition(position_idx);
   const bool includes_reference =
      std::find(symbols.begin(), symbols.end(), local_reference_symbol) != symbols.end();

   const bool includes_missing_symbol =
      std::find(symbols.begin(), symbols.end(), SymbolType::SYMBOL_MISSING) != symbols.end();

   if (includes_reference && includes_missing_symbol) {
      return compileWithMissingSymbolAndReference(
         sequence_column, position_idx, symbols, row_layout
      );
   }
   if (includes_missing_symbol) {
      return compileWithMissingSymbol(sequence_column, position_idx, symbols, row_layout);
   }
   if (includes_reference) {
      return compileWithReference(sequence_column, position_idx, symbols, row_layout);
   }
   return compileOnlyMutations(sequence_column, position_idx, symbols, row_layout);
}

template std::unique_ptr<filter::operators::Operator> compileSymbolInSet<AminoAcid>(
   const SequenceColumn<AminoAcid>& sequence_column,
   uint32_t position_idx,
   const std::vector<AminoAcid::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
);
template std::unique_ptr<filter::operators::Operator> compileSymbolInSet<Nucleotide>(
   const SequenceColumn<Nucleotide>& sequence_column,
   uint32_t position_idx,
   const std::vector<Nucleotide::Symbol>& symbols,
   const storage::column::RowLayout& row_layout
);

template <typename SymbolType>
std::unique_ptr<ScalarExpression> SymbolInSet<SymbolType>::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   throw QueryCompilationException(
      "Cannot rewrite SymbolInSet - this expression should only be created during query rewrites "
      "and not directly used"
   );
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> SymbolInSet<SymbolType>::compile(
   const storage::Table& table
) const {
   const auto valid_sequence_name = validateSequenceName<SymbolType>(column.name, *table.schema);

   const auto& sequence_column = table.getColumn<typename SymbolType::Column>(valid_sequence_name);

   return compileSymbolInSet(sequence_column, position_idx, symbols, table.row_layout);
}

template class SymbolInSet<AminoAcid>;
template class SymbolInSet<Nucleotide>;

}  // namespace rhydb::query_engine::scalar_expressions
