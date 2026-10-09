#include "rhydb/storage/reference_genomes.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

template <>
struct nlohmann::adl_serializer<rhydb::ReferenceGenomes> {
   // NOLINTNEXTLINE(readability-identifier-naming)
   static void to_json(
      nlohmann::json& js_object,
      const rhydb::ReferenceGenomes& reference_genomes
   ) {
      nlohmann::json nucleotide_sequences_json;
      for (size_t sequence_idx = 0;
           sequence_idx < reference_genomes.nucleotide_sequence_names.size();
           ++sequence_idx) {
         const auto& name = reference_genomes.nucleotide_sequence_names.at(sequence_idx);
         const auto& sequence = reference_genomes.raw_nucleotide_sequences.at(sequence_idx);
         nucleotide_sequences_json.push_back({{"name", name}, {"sequence", sequence}});
      }
      js_object["nucleotideSequences"] = std::move(nucleotide_sequences_json);

      nlohmann::json aa_sequences_json;
      for (size_t sequence_idx = 0; sequence_idx < reference_genomes.aa_sequence_names.size();
           ++sequence_idx) {
         const auto& name = reference_genomes.aa_sequence_names.at(sequence_idx);
         const auto& sequence = reference_genomes.raw_aa_sequences.at(sequence_idx);
         aa_sequences_json.push_back({{"name", name}, {"sequence", sequence}});
      }
      js_object["genes"] = std::move(aa_sequences_json);
   }
};

namespace rhydb {

ReferenceGenomes::ReferenceGenomes(
   const std::vector<std::pair<std::string, std::string>>& nucleotide_sequences_,
   const std::vector<std::pair<std::string, std::string>>& aa_sequences_
) {
   nucleotide_sequence_names.reserve(nucleotide_sequences_.size());
   raw_nucleotide_sequences.reserve(nucleotide_sequences_.size());
   for (const auto& [sequence_name, raw_nucleotide_sequence] : nucleotide_sequences_) {
      nucleotide_sequence_names.emplace_back(sequence_name);
      raw_nucleotide_sequences.emplace_back(raw_nucleotide_sequence);
   }
   aa_sequence_names.reserve(aa_sequences_.size());
   raw_aa_sequences.reserve(aa_sequences_.size());
   for (const auto& [sequence_name, raw_aa_sequence] : aa_sequences_) {
      aa_sequence_names.emplace_back(sequence_name);
      raw_aa_sequences.emplace_back(raw_aa_sequence);
   }
}

namespace {

ReferenceGenomes readFromJson(const std::filesystem::path& reference_genomes_path) {
   std::vector<std::pair<std::string, std::string>> nucleotide_sequences;
   std::vector<std::pair<std::string, std::string>> aa_sequences;
   nlohmann::json reference_genomes_json;
   std::ifstream(reference_genomes_path) >> reference_genomes_json;

   if (!reference_genomes_json.contains("nucleotideSequences")) {
      throw preprocessing::PreprocessingException(
         "Reference genomes file does not contain key 'nucleotideSequences'"
      );
   }
   if (!reference_genomes_json.contains("genes")) {
      throw preprocessing::PreprocessingException(
         "Reference genomes file does not contain key 'genes'"
      );
   }

   const nlohmann::json nuc_seq_json = reference_genomes_json["nucleotideSequences"];
   const nlohmann::json aa_seq_json = reference_genomes_json["genes"];

   for (const auto& [key, value] : nuc_seq_json.items()) {
      if (value.is_array()) {
         SPDLOG_INFO("Got array, expected object: " + value.dump());
         continue;
      }
      if (!value.contains("name") || !value["name"].is_string()) {
         SPDLOG_INFO(
            "Expected object to contain the key 'name' with string value, got: " + value.dump()
         );
         continue;
      }
      std::string name = value["name"].get<std::string>();
      if (std::ranges::all_of(name, [](char chr) { return isspace(chr); })) {
         SPDLOG_INFO("The name {} consists only of whitespace characters. Ignoring key.", name);
         continue;
      }
      if (!value.contains("sequence") || !value["sequence"].is_string()) {
         SPDLOG_INFO(
            "Expected object to contain the key 'sequence' with string value, got: " + value.dump()
         );
         continue;
      }

      nucleotide_sequences.emplace_back(value["name"], value["sequence"]);
   }

   for (const auto& [key, value] : aa_seq_json.items()) {
      if (value.is_array()) {
         SPDLOG_INFO("Got array, expected object: " + value.dump());
         continue;
      }
      if (!value.contains("name") || !value["name"].is_string()) {
         SPDLOG_INFO(
            "Expected object to contain the key 'name' with string value, got: " + value.dump()
         );
         continue;
      }
      std::string name = value["name"].get<std::string>();
      if (std::ranges::all_of(name, [](char chr) { return isspace(chr); })) {
         SPDLOG_INFO("The name {} consists only of whitespace characters. Ignoring key.", name);
         continue;
      }
      if (!value.contains("sequence") || !value["sequence"].is_string()) {
         SPDLOG_INFO(
            "Expected object to contain the key 'sequence' with string value, got: " + value.dump()
         );
         continue;
      }

      aa_sequences.emplace_back(value["name"], value["sequence"]);
   }
   return ReferenceGenomes{nucleotide_sequences, aa_sequences};
}

}  // namespace

ReferenceGenomes ReferenceGenomes::readFromFile(
   const std::filesystem::path& reference_genomes_path
) {
   EVOBENCH_SCOPE("ReferenceGenomes", "readFromFile");
   if (!std::filesystem::exists(reference_genomes_path)) {
      throw std::filesystem::filesystem_error(
         "Reference genomes file " + reference_genomes_path.string() + " does not exist",
         std::error_code()
      );
   }

   if (reference_genomes_path.extension() != ".json") {
      throw std::filesystem::filesystem_error(
         "Reference genomes file " + reference_genomes_path.string() + " is not a json file",
         std::error_code()
      );
   }

   SPDLOG_INFO("Read reference genomes from file: {}", reference_genomes_path.string());

   return readFromJson(reference_genomes_path);
}

void ReferenceGenomes::writeToFile(const std::filesystem::path& reference_genomes_path) const {
   if (reference_genomes_path.extension() != ".json") {
      throw std::filesystem::filesystem_error(
         "Reference genomes file " + reference_genomes_path.string() + " is not a json file",
         std::error_code()
      );
   }

   SPDLOG_INFO("Write reference genomes to file: {}", reference_genomes_path.string());
   const nlohmann::json reference_genomes_json = *this;
   std::ofstream(reference_genomes_path) << reference_genomes_json.dump(4);
}

template <>
std::vector<std::string> ReferenceGenomes::getSequenceNames<Nucleotide>() const {
   return nucleotide_sequence_names;
}

template <>
std::vector<std::string> ReferenceGenomes::getSequenceNames<AminoAcid>() const {
   return aa_sequence_names;
}

template <>
std::vector<std::vector<Nucleotide::Symbol>> ReferenceGenomes::getReferenceSequences<
   Nucleotide>() const {
   std::vector<std::vector<Nucleotide::Symbol>> result;
   result.reserve(raw_nucleotide_sequences.size());
   for (const auto& raw_nucleotide_sequence : raw_nucleotide_sequences) {
      result.emplace_back(stringToVector<Nucleotide>(raw_nucleotide_sequence));
   }
   return result;
}

template <>
std::vector<std::vector<AminoAcid::Symbol>> ReferenceGenomes::getReferenceSequences<
   AminoAcid>() const {
   std::vector<std::vector<AminoAcid::Symbol>> result;
   result.reserve(raw_aa_sequences.size());
   for (const auto& raw_nucleotide_sequence : raw_aa_sequences) {
      result.emplace_back(stringToVector<AminoAcid>(raw_nucleotide_sequence));
   }
   return result;
}

}  // namespace rhydb