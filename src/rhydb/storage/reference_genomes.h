#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace rhydb {

struct ReferenceGenomes {
   std::vector<std::string> nucleotide_sequence_names;
   std::vector<std::string> aa_sequence_names;
   std::vector<std::string> raw_nucleotide_sequences;
   std::vector<std::string> raw_aa_sequences;

   ReferenceGenomes() = default;

   explicit ReferenceGenomes(
      const std::vector<std::pair<std::string, std::string>>& nucleotide_sequences_,
      const std::vector<std::pair<std::string, std::string>>& aa_sequences_
   );

   void writeToFile(const std::filesystem::path& reference_genomes_path) const;

   static ReferenceGenomes readFromFile(const std::filesystem::path& reference_genomes_path);

   template <typename SymbolType>
   [[nodiscard]] std::vector<std::string> getSequenceNames() const;

   template <typename SymbolType>
   std::vector<std::vector<typename SymbolType::Symbol>> getReferenceSequences() const;

   template <typename SymbolType>
   static std::vector<typename SymbolType::Symbol> stringToVector(const std::string& string) {
      std::vector<typename SymbolType::Symbol> sequence_vector;

      for (const char character : string) {
         auto symbol = SymbolType::charToSymbol(character);

         if (!symbol.has_value()) {
            throw std::runtime_error(
               fmt::format(
                  "{} reference sequence with illegal {} code: {}",
                  SymbolType::SYMBOL_NAME,
                  SymbolType::SYMBOL_NAME_LOWER_CASE,
                  std::to_string(character)
               )
            );
         }

         sequence_vector.push_back(*symbol);
      }
      return sequence_vector;
   }

   template <typename SymbolType>
   static std::string vectorToString(const std::vector<typename SymbolType::Symbol>& vector) {
      std::string sequence_string;
      sequence_string.reserve(vector.size());

      for (const typename SymbolType::Symbol symbol : vector) {
         auto character = SymbolType::symbolToChar(symbol);
         sequence_string += character;
      }
      return sequence_string;
   }
};

}  // namespace rhydb
