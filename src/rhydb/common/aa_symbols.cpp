#include "rhydb/common/aa_symbols.h"

#include <algorithm>

#include "rhydb/common/symbol_map.h"

namespace rhydb {

// Primary definition: what each symbol codes for.
// Concrete amino acids code for themselves.
// Ambiguity symbols code for the set of amino acids they represent per IUPAC.
// X codes for all symbols (including GAP, STOP, and other ambiguity codes).
const rhydb::SymbolMap<AminoAcid, std::vector<AminoAcid::Symbol>> AminoAcid::CODES_FOR{{{
   {Symbol::GAP},           // GAP
   {Symbol::A},             // Alanine
   {Symbol::C},             // Cysteine
   {Symbol::D},             // Aspartic Acid
   {Symbol::E},             // Glutamic Acid
   {Symbol::F},             // Phenylalanine
   {Symbol::G},             // Glycine
   {Symbol::H},             // Histidine
   {Symbol::I},             // Isoleucine
   {Symbol::K},             // Lysine
   {Symbol::L},             // Leucine
   {Symbol::M},             // Methionine
   {Symbol::N},             // Asparagine
   {Symbol::O},             // Pyrrolysine
   {Symbol::P},             // Proline
   {Symbol::Q},             // Glutamine
   {Symbol::R},             // Arginine
   {Symbol::S},             // Serine
   {Symbol::T},             // Threonine
   {Symbol::U},             // Selenocysteine
   {Symbol::V},             // Valine
   {Symbol::W},             // Tryptophan
   {Symbol::Y},             // Tyrosine
   {Symbol::D, Symbol::N},  // B - Aspartic acid or Asparagine
   {Symbol::L, Symbol::I},  // J - Leucine or Isoleucine
   {Symbol::Q, Symbol::E},  // Z - Glutamine or Glutamic acid
   {Symbol::STOP},          // STOP
   // X codes for every symbol
   {Symbol::GAP, Symbol::A, Symbol::C, Symbol::D, Symbol::E, Symbol::F,    Symbol::G,
    Symbol::H,   Symbol::I, Symbol::K, Symbol::L, Symbol::M, Symbol::N,    Symbol::O,
    Symbol::P,   Symbol::Q, Symbol::R, Symbol::S, Symbol::T, Symbol::U,    Symbol::V,
    Symbol::W,   Symbol::Y, Symbol::B, Symbol::J, Symbol::Z, Symbol::STOP, Symbol::X,},
},},};

namespace {
rhydb::SymbolMap<AminoAcid, std::vector<AminoAcid::Symbol>> deriveAmbiguitySymbols() {
   rhydb::SymbolMap<AminoAcid, std::vector<AminoAcid::Symbol>> result;
   for (auto symbol : AminoAcid::SYMBOLS) {
      const auto& codes_for_symbol = AminoAcid::CODES_FOR.at(symbol);
      std::vector<AminoAcid::Symbol> ambiguous;
      for (auto candidate : AminoAcid::SYMBOLS) {
         const auto& codes_for_candidate = AminoAcid::CODES_FOR.at(candidate);
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

const rhydb::SymbolMap<AminoAcid, std::vector<AminoAcid::Symbol>> AminoAcid::AMBIGUITY_SYMBOLS =
   deriveAmbiguitySymbols();

}  // namespace rhydb
