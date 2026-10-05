#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <re2/re2.h>
#include <boost/serialization/access.hpp>
#include <roaring/roaring.hh>

namespace rhydb::storage::insertion {

template <typename SymbolType>
class ThreeMerHash {
  public:
   size_t operator()(const std::array<typename SymbolType::Symbol, 3>& three_mer) const;
};

template <typename SymbolType>
class InsertionSearchPattern {
   std::vector<std::array<typename SymbolType::Symbol, 3>> three_mers;
   std::unique_ptr<const re2::RE2> regex;

   InsertionSearchPattern(
      std::vector<std::array<typename SymbolType::Symbol, 3>> three_mers,
      std::unique_ptr<const re2::RE2> regex
   );

  public:
   [[nodiscard]] static InsertionSearchPattern make(const std::string& search_pattern);

   [[nodiscard]] const std::vector<std::array<typename SymbolType::Symbol, 3>>& getThreeMers(
   ) const;

   [[nodiscard]] const re2::RE2& getRegex() const;
};

using InsertionIds = std::vector<uint32_t>;

class Insertion {
  private:
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & value;
      archive & row_ids;
      // clang-format on
   }

  public:
   std::string value;
   roaring::Roaring row_ids;
};

template <typename SymbolType>
class InsertionPosition {
  private:
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
         archive & insertions;
         archive & three_mer_index;
      // clang-format on
   }

  public:
   std::vector<Insertion> insertions;

   using ThreeMerType = std::unordered_map<
      std::array<typename SymbolType::Symbol, 3>,
      InsertionIds,
      ThreeMerHash<SymbolType>>;

   ThreeMerType three_mer_index;

   [[nodiscard]] std::unique_ptr<roaring::Roaring> searchWithThreeMerIndex(
      const std::vector<std::array<typename SymbolType::Symbol, 3>>& search_three_mers,
      const re2::RE2& search_pattern
   ) const;

   [[nodiscard]] std::unique_ptr<roaring::Roaring> searchWithRegex(
      const re2::RE2& regex_search_pattern
   ) const;

   void buildThreeMerIndex();

   [[nodiscard]] std::unique_ptr<roaring::Roaring> search(
      const InsertionSearchPattern<SymbolType>& search_pattern
   ) const;
};

template <typename SymbolType>
class InsertionIndex {
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & insertion_positions;
      archive & collected_insertions;
      // clang-format on
   }

   std::unordered_map<uint32_t, InsertionPosition<SymbolType>> insertion_positions;
   std::unordered_map<uint32_t, std::unordered_map<std::string, roaring::Roaring>>
      collected_insertions;

  public:
   void addLazily(uint32_t position_idx, const std::string& insertion, uint32_t row_id);

   void buildIndex();

   [[nodiscard]] const std::unordered_map<uint32_t, InsertionPosition<SymbolType>>&
   getInsertionPositions() const;

   [[nodiscard]] std::unique_ptr<roaring::Roaring> search(
      uint32_t position_idx,
      const InsertionSearchPattern<SymbolType>& search_pattern
   ) const;
};

}  // namespace rhydb::storage::insertion
