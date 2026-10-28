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
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>

#include "rhydb/common/bitmap.h"
#include "rhydb/roaring_util/roaring_container.h"

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

   [[nodiscard]] const std::vector<std::array<typename SymbolType::Symbol, 3>>&
   getThreeMers() const;

   [[nodiscard]] const re2::RE2& getRegex() const;
};

using InsertionIds = std::vector<uint32_t>;

/// The rows of one chunk that hold an insertion, as rows within the chunk.
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
   roaring_util::RoaringContainer row_ids = roaring_util::RoaringContainer::withCapacity(0);
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

   [[nodiscard]] roaring_util::RoaringContainer searchWithThreeMerIndex(
      const std::vector<std::array<typename SymbolType::Symbol, 3>>& search_three_mers,
      const re2::RE2& search_pattern
   ) const;

   [[nodiscard]] roaring_util::RoaringContainer searchWithRegex(
      const re2::RE2& regex_search_pattern
   ) const;

   void buildThreeMerIndex();

   [[nodiscard]] roaring_util::RoaringContainer search(
      const InsertionSearchPattern<SymbolType>& search_pattern
   ) const;
};

/// The insertions of the rows of one chunk, by position.
template <typename SymbolType>
class InsertionIndexChunk {
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & insertion_positions;
      // clang-format on
   }

   std::unordered_map<uint32_t, InsertionPosition<SymbolType>> insertion_positions;

  public:
   /// Builds the index from the rows holding each insertion value at each position.
   [[nodiscard]] static InsertionIndexChunk build(
      std::unordered_map<
         uint32_t,
         std::unordered_map<std::string, roaring_util::RoaringContainer>>&& rows_by_insertion
   );

   [[nodiscard]] const std::unordered_map<uint32_t, InsertionPosition<SymbolType>>&
   getInsertionPositions() const {
      return insertion_positions;
   }

   /// The rows of this chunk holding an insertion at `position_idx` that matches `search_pattern`.
   [[nodiscard]] roaring_util::RoaringContainer search(
      uint32_t position_idx,
      const InsertionSearchPattern<SymbolType>& search_pattern
   ) const;
};

/// The insertions of a sequence column: one `InsertionIndexChunk` per chunk of the column.
template <typename SymbolType>
class InsertionIndex {
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & chunks;
      // clang-format on
   }

   /// One entry per chunk, indexed by chunk id.
   std::vector<InsertionIndexChunk<SymbolType>> chunks;
   /// The insertions added since the last `appendChunk`, by position and value: the rows of the
   /// chunk being appended holding them.
   std::unordered_map<uint32_t, std::unordered_map<std::string, roaring_util::RoaringContainer>>
      collected_insertions;

  public:
   /// Adds that the row `row_in_chunk` of the chunk being appended holds `insertion` at
   /// `position_idx`.
   void add(uint32_t position_idx, const std::string& insertion, uint16_t row_in_chunk);

   /// Builds the index of the chunk being appended from the insertions added since the last call.
   void appendChunk();

   [[nodiscard]] const std::vector<InsertionIndexChunk<SymbolType>>& getChunks() const {
      return chunks;
   }

   /// The rows holding an insertion at `position_idx` that matches `search_pattern`.
   [[nodiscard]] Bitmap search(
      uint32_t position_idx,
      const InsertionSearchPattern<SymbolType>& search_pattern
   ) const;
};

}  // namespace rhydb::storage::insertion
