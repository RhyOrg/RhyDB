#include "rhydb/query_engine/scalar_expressions/mutation_profile.h"

#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/query_engine/query_parse_sequence_name.h"
#include "rhydb/query_engine/scalar_expressions/negation.h"
#include "rhydb/query_engine/scalar_expressions/nof.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/horizontal_coverage_index.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
MutationProfile<SymbolType>::MutationProfile(
   schema::ColumnIdentifier column,
   uint32_t distance,
   ProfileInput input
)
    : column(std::move(column)),
      distance(distance),
      input(std::move(input)) {}

template <typename SymbolType>
std::string MutationProfile<SymbolType>::toString() const {
   const std::string seq_prefix = column.name + ":";
   const std::string input_str = std::visit(
      [](const auto& inp) -> std::string {
         using T = std::decay_t<decltype(inp)>;
         if constexpr (std::is_same_v<T, QuerySequenceInput>) {
            return "querySequence=" + inp.sequence.substr(0, 20) + "...";
         } else if constexpr (std::is_same_v<T, SequenceIdInput>) {
            return "sequenceId=" + inp.id;
         } else {
            return "mutations(count=" + std::to_string(inp.mutations.size()) + ")";
         },
      },
      input
   );
   return fmt::format("MutationProfile({}distance={},{})", seq_prefix, distance, input_str);
}

template <typename SymbolType>
std::vector<schema::ColumnIdentifier> MutationProfile<SymbolType>::freeIUs() const {
   return {column};
}

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> MutationProfile<SymbolType>::buildProfileFromQuerySequence(
   const storage::column::SequenceColumn<SymbolType>& sequence_column
) const {
   const auto& query_sequence = std::get<QuerySequenceInput>(input).sequence;
   const size_t ref_len = sequence_column.metadata->reference_sequence.size();
   CHECK_RHYDB_QUERY(
      query_sequence.size() == ref_len,
      "querySequence length {} does not match the reference sequence length {} for {} "
      "MutationProfile",
      query_sequence.size(),
      ref_len,
      SymbolType::SYMBOL_NAME
   );

   std::vector<typename SymbolType::Symbol> profile;
   profile.reserve(ref_len);
   for (char character : query_sequence) {
      const auto symbol = SymbolType::charToSymbol(character);
      CHECK_RHYDB_QUERY(
         symbol.has_value(),
         "Invalid {} symbol '{}' in querySequence for MutationProfile",
         SymbolType::SYMBOL_NAME,
         character
      );
      profile.push_back(symbol.value());
   }
   return profile;
}

using storage::column::RowId;

namespace {

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> reconstructSequenceAtRow(
   const storage::column::SequenceColumn<SymbolType>& sequence_column,
   RowId row_id
) {
   roaring::Roaring single_row_ids;
   single_row_ids.add(row_id.toGlobal());
   const Bitmap single_row{std::move(single_row_ids)};

   std::vector<std::string> sequences = {sequence_column.local_reference_sequence_string};
   sequence_column.vertical_sequence_index.overwriteSymbolsInSequences(sequences, single_row);
   sequence_column.horizontal_coverage_index.template overwriteCoverageInSequence<SymbolType>(
      sequences, single_row
   );

   std::vector<typename SymbolType::Symbol> profile;
   profile.reserve(sequences[0].size());
   for (const char character : sequences[0]) {
      const auto sym = SymbolType::charToSymbol(character);
      RHYDB_ASSERT(sym.has_value());
      profile.push_back(sym.value());
   }
   return profile;
}

}  // namespace

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> MutationProfile<SymbolType>::buildProfileFromSequenceId(
   const storage::Table& table,
   const std::string& valid_sequence_name
) const {
   const auto& seq_id = std::get<SequenceIdInput>(input).id;
   CHECK_RHYDB_QUERY(
      table.schema->primary_key.has_value(),
      "{} MutationProfile sequenceId lookup requires the table to declare a primary key",
      SymbolType::SYMBOL_NAME
   );
   const auto& primary_key_name = table.schema->primary_key->name;
   const auto primary_key_type = table.schema->primary_key->type;

   const auto& seq_col =
      table.columns.getColumns<typename SymbolType::Column>().at(valid_sequence_name);

   std::optional<RowId> found_row_id;

   if (primary_key_type == schema::ColumnType::STRING) {
      const auto& primary_key_column = table.columns.string_columns.at(primary_key_name);
      for (const RowId row_id : table.row_layout) {
         if (primary_key_column.getValueString(row_id) == seq_id) {
            found_row_id = row_id;
            break;
         }
      }
   } else if (primary_key_type == schema::ColumnType::DICTIONARY_ENCODED) {
      const auto& primary_key_column =
         table.columns.dictionary_encoded_columns.at(primary_key_name);
      const auto bitmap_opt = primary_key_column.filter(std::optional<std::string>(seq_id));
      if (bitmap_opt.has_value() && !bitmap_opt.value()->isEmpty()) {
         found_row_id = RowId::fromGlobal(bitmap_opt.value()->minimum());
      }
   } else {
      throw IllegalQueryException(fmt::format(
         "Unsupported primary key column type for {} MutationProfile sequenceId lookup",
         SymbolType::SYMBOL_NAME
      ));
   }

   if (found_row_id.has_value()) {
      return reconstructSequenceAtRow<SymbolType>(seq_col, found_row_id.value());
   }

   CHECK_RHYDB_QUERY(
      false,
      "No sequence found with primary key '{}' in {} MutationProfile",
      seq_id,
      SymbolType::SYMBOL_NAME
   );
   RHYDB_UNREACHABLE();
}

template <typename SymbolType>
std::vector<typename SymbolType::Symbol> MutationProfile<SymbolType>::buildProfileFromMutations(
   const storage::column::SequenceColumn<SymbolType>& sequence_column
) const {
   const auto& mutation_list = std::get<MutationsInput>(input).mutations;
   const size_t ref_len = sequence_column.metadata->reference_sequence.size();

   // Start with a copy of the reference sequence
   std::vector<typename SymbolType::Symbol> profile(
      sequence_column.metadata->reference_sequence.begin(),
      sequence_column.metadata->reference_sequence.end()
   );

   for (const auto& mutation : mutation_list) {
      CHECK_RHYDB_QUERY(
         mutation.position_idx < ref_len,
         "{} MutationProfile mutation position {} is out of bounds (reference length {})",
         SymbolType::SYMBOL_NAME,
         mutation.position_idx + 1,
         ref_len
      );
      profile[mutation.position_idx] = mutation.symbol;
   }
   return profile;
}

template <typename SymbolType>
std::unique_ptr<ScalarExpression> MutationProfile<SymbolType>::rewrite(
   const storage::Table& table,
   AmbiguityMode /*mode*/
) const {
   const auto valid_sequence_name = validateSequenceName<SymbolType>(column.name, *table.schema);

   const auto& sequence_column =
      table.columns.getColumns<typename SymbolType::Column>().at(valid_sequence_name);

   // Build the profile sequence
   std::vector<typename SymbolType::Symbol> profile;
   if (std::holds_alternative<QuerySequenceInput>(input)) {
      profile = buildProfileFromQuerySequence(sequence_column);
   } else if (std::holds_alternative<SequenceIdInput>(input)) {
      profile = buildProfileFromSequenceId(table, valid_sequence_name);
   } else {
      profile = buildProfileFromMutations(sequence_column);
   }

   // For each position, build a "difference" child expression:
   // difference at pos = SymbolInSet(seq_name, pos, symbols NOT compatible with profile[pos])
   // where "compatible" means: symbols in AMBIGUITY_SYMBOLS[profile[pos]]
   ScalarExpressionVector difference_children;
   for (size_t pos = 0; pos < profile.size(); ++pos) {
      const auto profile_symbol = profile[pos];

      // Skip positions where the profile has the missing/unknown symbol
      if (profile_symbol == SymbolType::SYMBOL_MISSING) {
         continue;
      }

      // Compute symbols that are NOT compatible with profile_symbol
      // (i.e., symbols that count as "definitely different" from profile_symbol)
      const auto& compatible_symbols = SymbolType::AMBIGUITY_SYMBOLS.at(profile_symbol);
      std::vector<typename SymbolType::Symbol> difference_symbols;
      for (const auto sym : SymbolType::SYMBOLS) {
         if (std::find(compatible_symbols.begin(), compatible_symbols.end(), sym) ==
             compatible_symbols.end()) {
            difference_symbols.push_back(sym);
         }
      }

      if (difference_symbols.empty()) {
         continue;
      }

      difference_children.push_back(std::make_unique<SymbolInSet<SymbolType>>(
         column, static_cast<uint32_t>(pos), std::move(difference_symbols)
      ));
   }

   // Return Not(NOf(difference_children, distance+1, false))
   // = "at most 'distance' differences" (conservative)
   auto at_least_distance_plus_one = std::make_unique<NOf>(
      std::move(difference_children),
      static_cast<int>(distance) + 1,
      /*match_exactly=*/false
   );
   return std::make_unique<Negation>(std::move(at_least_distance_plus_one));
}

template <typename SymbolType>
std::unique_ptr<filter::operators::Operator> MutationProfile<SymbolType>::compile(
   const storage::Table& /*table*/
) const {
   throw QueryCompilationException{
      "{} MutationProfile expression must be eliminated in the query rewrite phase",
      SymbolType::SYMBOL_NAME,
   };
}

template class MutationProfile<AminoAcid>;
template class MutationProfile<Nucleotide>;

}  // namespace rhydb::query_engine::scalar_expressions
