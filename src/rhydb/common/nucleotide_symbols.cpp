#include "rhydb/common/nucleotide_symbols.h"

#include <algorithm>

namespace rhydb {

// Primary definition: what each symbol codes for.
// Concrete symbols code for themselves. Ambiguity symbols code for
// the set of concrete symbols they represent per IUPAC conventions.
// N codes for all symbols (including GAP and other ambiguity codes).
const rhydb::SymbolMap<Nucleotide, std::vector<Nucleotide::Symbol>> Nucleotide::CODES_FOR{{{
   {Symbol::GAP},                      // GAP
   {Symbol::A},                        // A - Adenine
   {Symbol::C},                        // C - Cytosine
   {Symbol::G},                        // G - Guanine
   {Symbol::T},                        // T - Thymine
   {Symbol::A, Symbol::G},             // R - puRine
   {Symbol::C, Symbol::T},             // Y - pYrimidine
   {Symbol::G, Symbol::C},             // S - Strong
   {Symbol::A, Symbol::T},             // W - Weak
   {Symbol::G, Symbol::T},             // K - Keto
   {Symbol::A, Symbol::C},             // M - aMino
   {Symbol::C, Symbol::G, Symbol::T},  // B - not A
   {Symbol::A, Symbol::G, Symbol::T},  // D - not C
   {Symbol::A, Symbol::C, Symbol::T},  // H - not G
   {Symbol::A, Symbol::C, Symbol::G},  // V - not T
   // N codes for every symbol
   {Symbol::GAP,
    Symbol::A,
    Symbol::C,
    Symbol::G,
    Symbol::T,
    Symbol::R,
    Symbol::Y,
    Symbol::S,
    Symbol::W,
    Symbol::K,
    Symbol::M,
    Symbol::B,
    Symbol::D,
    Symbol::H,
    Symbol::V,
    Symbol::N,},
},},};

namespace {
rhydb::SymbolMap<Nucleotide, std::vector<Nucleotide::Symbol>> deriveAmbiguitySymbols() {
   rhydb::SymbolMap<Nucleotide, std::vector<Nucleotide::Symbol>> result;
   for (auto symbol : Nucleotide::SYMBOLS) {
      const auto& codes_for_symbol = Nucleotide::CODES_FOR.at(symbol);
      std::vector<Nucleotide::Symbol> ambiguous;
      for (auto candidate : Nucleotide::SYMBOLS) {
         const auto& codes_for_candidate = Nucleotide::CODES_FOR.at(candidate);
         // candidate is ambiguous for symbol if CODES_FOR[symbol] ⊆ CODES_FOR[candidate]
         const bool is_superset = std::ranges::all_of(codes_for_symbol, [&](auto coded_symbol) {
            return std::ranges::find(codes_for_candidate, coded_symbol) !=
                   codes_for_candidate.end();
         });
         if (is_superset) {
            ambiguous.push_back(candidate);
         }
      }
      result[symbol] = std::move(ambiguous);
   }
   return result;
}
}  // namespace

const rhydb::SymbolMap<Nucleotide, std::vector<Nucleotide::Symbol>> Nucleotide::AMBIGUITY_SYMBOLS =
   deriveAmbiguitySymbols();

}  // namespace rhydb
