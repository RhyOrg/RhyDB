#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rhydb/common/symbol_map.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb {

namespace storage::column {
template <typename SymbolType>
class SequenceColumn;
}

class Nucleotide {
  public:
   /// https://www.bioinformatics.org/sms2/iupac.html
   enum class Symbol : char {
      GAP,  // -, GAP
      A,    // Adenine
      C,    // Cytosine
      G,    // Guanine
      T,    // (or U) Thymine (or Uracil)
      R,    // A or G
      Y,    // C or T
      S,    // G or C
      W,    // A or T
      K,    // G or T
      M,    // A or C
      B,    // C or G or T
      D,    // A or G or T
      H,    // A or C or T
      V,    // A or C or G
      N,    // any base
   };

   static constexpr schema::ColumnType COLUMN_TYPE = schema::ColumnType::NUCLEOTIDE_SEQUENCE;
   static constexpr schema::ValueType VALUE_TYPE = schema::ValueType::NUCLEOTIDE_SEQUENCE;
   using Column = storage::column::SequenceColumn<Nucleotide>;

   static constexpr uint32_t COUNT = 16;

   static constexpr std::string_view SYMBOL_NAME = "Nucleotide";
   static constexpr std::string_view SYMBOL_NAME_LOWER_CASE = "nucleotide";
   static constexpr std::string_view PREFIX = "nuc_";

   static constexpr std::array<Symbol, COUNT> SYMBOLS{
      Symbol::GAP,
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
      Symbol::N,
   };

   static constexpr std::array<Symbol, 5> VALID_MUTATION_SYMBOLS{
      Symbol::GAP,
      Symbol::A,
      Symbol::C,
      Symbol::G,
      Symbol::T,
   };

   static constexpr std::array<Symbol, 11> INVALID_MUTATION_SYMBOLS{
      Symbol::R,  // A or G
      Symbol::Y,  // C or T
      Symbol::S,  // G or C
      Symbol::W,  // A or T
      Symbol::K,  // G or T
      Symbol::M,  // A or C
      Symbol::B,  // C or G or T
      Symbol::D,  // A or G or T
      Symbol::H,  // A or C or T
      Symbol::V,  // A or C or G
      Symbol::N,  // any base
   };

   static_assert(INVALID_MUTATION_SYMBOLS.size() + VALID_MUTATION_SYMBOLS.size() == SYMBOLS.size());

   /// Maps each symbol to the set of symbols it codes for.
   /// The mapped set may include both concrete bases (GAP, A, C, G, T) and
   /// ambiguity symbols (R, Y, ..., N), and can include the symbol itself.
   /// For example, N codes for all nucleotide symbols.
   static const rhydb::SymbolMap<Nucleotide, std::vector<Nucleotide::Symbol>> CODES_FOR;

   /// Maps each symbol to all symbols whose CODES_FOR set is a superset.
   /// I.e. AMBIGUITY_SYMBOLS[S] = {Y : CODES_FOR[S] ⊆ CODES_FOR[Y]}.
   /// Derived from CODES_FOR.
   static const rhydb::SymbolMap<Nucleotide, std::vector<Nucleotide::Symbol>> AMBIGUITY_SYMBOLS;

   static constexpr Symbol SYMBOL_MISSING = Symbol::N;

   static char symbolToChar(Nucleotide::Symbol symbol) {
      switch (symbol) {
         case Symbol::GAP:
            return '-';
         case Symbol::A:
            return 'A';
         case Symbol::C:
            return 'C';
         case Symbol::G:
            return 'G';
         case Symbol::T:
            return 'T';
         case Symbol::R:
            return 'R';
         case Symbol::Y:
            return 'Y';
         case Symbol::S:
            return 'S';
         case Symbol::W:
            return 'W';
         case Symbol::K:
            return 'K';
         case Symbol::M:
            return 'M';
         case Symbol::B:
            return 'B';
         case Symbol::D:
            return 'D';
         case Symbol::H:
            return 'H';
         case Symbol::V:
            return 'V';
         case Symbol::N:
            return 'N';
      }
      RHYDB_UNREACHABLE();
   }

   static constexpr std::optional<Nucleotide::Symbol> charToSymbolSwitch(char character) {
      switch (character) {
         case '-':
            return Symbol::GAP;
         case 'A':
         case 'a':
            return Symbol::A;
         case 'C':
         case 'c':
            return Symbol::C;
         case 'G':
         case 'g':
            return Symbol::G;
         case 'T':
         case 'U':
         case 't':
         case 'u':
            return Symbol::T;
         case 'R':
         case 'r':
            return Symbol::R;
         case 'Y':
         case 'y':
            return Symbol::Y;
         case 'S':
         case 's':
            return Symbol::S;
         case 'W':
         case 'w':
            return Symbol::W;
         case 'K':
         case 'k':
            return Symbol::K;
         case 'M':
         case 'm':
            return Symbol::M;
         case 'B':
         case 'b':
            return Symbol::B;
         case 'D':
         case 'd':
            return Symbol::D;
         case 'H':
         case 'h':
            return Symbol::H;
         case 'V':
         case 'v':
            return Symbol::V;
         case 'N':
         case 'n':
            return Symbol::N;
         default:
            return std::nullopt;
      }
   }

   static constexpr std::optional<Nucleotide::Symbol> charToSymbol(char character);

   static std::string sequenceToString(const std::vector<Symbol>& reference_sequence) {
      std::string result;
      std::ranges::transform(reference_sequence, std::back_inserter(result), symbolToChar);
      return result;
   }
};

/// The switch in charToSymbolSwitch compiles to an indirect jump, which is mispredicted for
/// every base of every inserted sequence. The table turns it into a single L1 load.
inline constexpr std::array<std::optional<Nucleotide::Symbol>, 256> NUCLEOTIDE_CHAR_TO_SYMBOL =
   [] {
      std::array<std::optional<Nucleotide::Symbol>, 256> table{};
      for (size_t character = 0; character < table.size(); ++character) {
         table[character] = Nucleotide::charToSymbolSwitch(static_cast<char>(character));
      }
      return table;
   }();

constexpr std::optional<Nucleotide::Symbol> Nucleotide::charToSymbol(char character) {
   return NUCLEOTIDE_CHAR_TO_SYMBOL[static_cast<unsigned char>(character)];
}

}  // namespace rhydb
