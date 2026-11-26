#include "rhydb/query_engine/filter/operators/has_symbol_in_set.h"

#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "evobench/evobench.hpp"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

namespace rhydb::query_engine::filter::operators {

HasSymbolInSet::HasSymbolInSet(
   const Chunks* chunks,
   uint32_t position_idx,
   storage::sequence::SymbolSet symbols,
   bool with_null_rows
)
    : chunks(chunks),
      position_idx(position_idx),
      symbols(symbols),
      with_null_rows(with_null_rows) {}

HasSymbolInSet::~HasSymbolInSet() noexcept = default;

std::string HasSymbolInSet::toString() const {
   std::vector<std::string> symbol_names;
   if (chunks->empty()) {
      for (uint8_t symbol = 0; symbol < storage::sequence::SequenceReference::MAX_SYMBOLS;
           ++symbol) {
         if (symbols.contains(symbol)) {
            symbol_names.push_back(fmt::format("#{}", symbol));
         }
      }
   } else {
      const auto& reference = chunks->front()->reference();
      for (uint8_t symbol = 0; symbol < reference.numSymbols(); ++symbol) {
         if (symbols.contains(symbol)) {
            symbol_names.emplace_back(1, reference.symbolChar(symbol));
         }
      }
   }
   return fmt::format(
      "HasSymbolInSet({}, {{{}}}{})",
      position_idx,
      fmt::join(symbol_names, ", "),
      with_null_rows ? ", with null rows" : ""
   );
}

bool HasSymbolInSet::match(storage::column::RowId row_id) const {
   const auto& chunk = *chunks->at(row_id.chunk_id);
   const uint16_t row = row_id.row_in_chunk;
   if (chunk.isNull(row)) {
      return with_null_rows;
   }
   const auto& reference = chunk.reference();
   if (!chunk.coverage().covers(row, position_idx)) {
      return symbols.contains(reference.missingSymbol());
   }
   const auto& mutations = chunk.mutations();
   for (const auto& entry : mutations.entriesAtPosition(position_idx)) {
      if (mutations.container(entry).contains(row)) {
         return symbols.contains(entry.symbol);
      }
   }
   return symbols.contains(reference.symbolAt(position_idx));
}

roaring_util::RoaringContainer HasSymbolInSet::makeChunk(
   uint16_t chunk_id,
   uint32_t chunk_size
) const {
   EVOBENCH_SCOPE("HasSymbolInSet", "makeChunk");
   const auto& chunk = *chunks->at(chunk_id);
   const auto all_rows = roaring_util::RoaringContainer::ofRange(0, chunk_size);
   auto result = storage::sequence::rowsWithSymbolsAt(
      chunk, position_idx, symbols, roaring_util::RoaringContainerView{all_rows}
   );
   if (with_null_rows) {
      result |= chunk.nullRows();
   }
   return result;
}

std::unique_ptr<Predicate> HasSymbolInSet::copy() const {
   return std::make_unique<HasSymbolInSet>(chunks, position_idx, symbols, with_null_rows);
}

std::unique_ptr<Predicate> HasSymbolInSet::negate() const {
   storage::sequence::SymbolSet negated_symbols;
   for (uint8_t symbol = 0; symbol < storage::sequence::SequenceReference::MAX_SYMBOLS; ++symbol) {
      if (!symbols.contains(symbol)) {
         negated_symbols.add(symbol);
      }
   }
   return std::make_unique<HasSymbolInSet>(chunks, position_idx, negated_symbols, !with_null_rows);
}

}  // namespace rhydb::query_engine::filter::operators
