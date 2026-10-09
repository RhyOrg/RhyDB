#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <boost/serialization/access.hpp>
#include <boost/serialization/split_free.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/horizontal_coverage_index.h"
#include "rhydb/storage/column/insertion_index.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/vertical_sequence_index.h"
#include "rhydb/zstd/zstd_decompressor.h"

namespace rhydb::storage::column {

template <typename SymbolType>
class SequenceColumnBuilder;

class SequenceColumnInfo {
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
      archive & sequence_count;
      archive & vertical_bitmaps_size;
      archive & horizontal_bitmaps_size;
   }

  public:
   uint32_t sequence_count;
   uint64_t vertical_bitmaps_size;
   uint64_t horizontal_bitmaps_size;
};

template <typename SymbolType>
class SequenceColumnMetadata : public ColumnMetadata {
  public:
   std::vector<typename SymbolType::Symbol> reference_sequence;

   explicit SequenceColumnMetadata(
      std::string column_name,
      std::vector<typename SymbolType::Symbol>&& reference_sequence
   );
};

template <typename SymbolType>
class SequenceColumn {
  public:
   using Metadata = SequenceColumnMetadata<SymbolType>;
   using Builder = SequenceColumnBuilder<SymbolType>;

   /// Compact, reference-diffed representation of a single sequence row. The
   /// builder diffs each inserted sequence against the column's (possibly
   /// adapted) local reference at insert time, so only the differences -- the
   /// covered range, the missing positions within it, and the mutations -- are
   /// buffered for the chunk. The full sequence is never retained, keeping
   /// per-chunk memory bounded regardless of genome length. appendChunk assigns
   /// the global row id and populates the column's indexes from this.
   struct BufferedSequence {
      bool is_null = false;
      Coverage coverage;
      Mutations<SymbolType> mutations;
      Insertions insertions;
   };
   using Buffer = std::vector<BufferedSequence>;

   static constexpr schema::ColumnType TYPE = SymbolType::COLUMN_TYPE;
   static constexpr schema::ValueType type() { return SymbolType::VALUE_TYPE; }
   using value_type = void;

  private:
   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
      // clang-format off
      archive & local_reference_sequence_string;
      archive & vertical_sequence_index;
      archive & horizontal_coverage_index;
      archive & insertion_index;
      archive & sequence_column_info;
      archive & sequence_count;
      archive & null_bitmap;
      archive & num_chunks;
      // clang-format on
   }

  public:
   const SequenceColumnMetadata<SymbolType>* metadata;
   const size_t genome_length;

   // Store the local reference sequence as a string to speed up insertions
   std::string local_reference_sequence_string;

   VerticalSequenceIndex<SymbolType> vertical_sequence_index;
   HorizontalCoverageIndex horizontal_coverage_index;
   insertion::InsertionIndex<SymbolType> insertion_index;
   SequenceColumnInfo sequence_column_info;
   roaring::Roaring null_bitmap;
   uint32_t sequence_count = 0;
   /// Number of appended chunks; see `RowLayout`. Kept so the column satisfies the `Column` concept
   /// and the table can check every column was appended to in lockstep.
   uint16_t num_chunks = 0;

   explicit SequenceColumn(Metadata* metadata);

   [[nodiscard]] size_t numChunks() const { return num_chunks; }

   /// Number of rows in ingestion chunk `chunk_id`. The shared `RowLayout` is the authoritative
   /// source, but the sequence column tracks the same per-chunk sizes in its coverage index (every
   /// row, null or not, contributes a coverage entry) so it satisfies the `Column` concept.
   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const {
      return horizontal_coverage_index.chunkSize(chunk_id);
   }

   [[nodiscard]] std::vector<typename SymbolType::Symbol> getLocalReference() const {
      std::vector<typename SymbolType::Symbol> local_reference;
      for (const char character : local_reference_sequence_string) {
         local_reference.push_back(SymbolType::charToSymbol(character).value());
      };
      return local_reference;
   }

   [[nodiscard]] SymbolType::Symbol getLocalReferencePosition(size_t position) const {
      RHYDB_ASSERT(position < metadata->reference_sequence.size());
      return SymbolType::charToSymbol(local_reference_sequence_string.at(position)).value();
   }

   [[nodiscard]] SequenceColumnInfo getInfo() const;

   /// Append a finalized ingestion chunk to the column's global structures,
   /// assigning global row ids as it goes.
   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   [[nodiscard]] bool isNull(RowId row_id) const;

   void finalize();

  private:
   static constexpr size_t BUFFER_SIZE = 1024;
   std::vector<SymbolMap<SymbolType, std::vector<uint32_t>>> mutation_buffer;

   // Population of a single pre-diffed row into the global structures at the given sparse global
   // row id (`chunk_id << 16 | row_in_chunk`): records its coverage, mutations and insertions.
   void appendValue(RowId row_id, const BufferedSequence& buffered);

   void appendNullValue(RowId row_id);

   void fillIndexes();

   void optimizeBitmaps();

   void flushBuffer();

   [[nodiscard]] SequenceColumnInfo calculateInfo();

   [[nodiscard]] size_t computeVerticalBitmapsSize() const;

   [[nodiscard]] size_t computeHorizontalBitmapsSize() const;
};

/// Buffers one ingestion chunk by diffing each inserted sequence against the
/// column's (possibly adapted) local reference and storing only the differences
/// (covered range, missing positions, mutations). The full sequences are never
/// retained, so per-chunk memory stays bounded regardless of genome length.
template <typename SymbolType>
class SequenceColumnBuilder {
   SequenceColumn<SymbolType>::Buffer buffer;

   // The column's current local reference (possibly adapted by a previous
   // finalize); inserted sequences are diffed against this so newly buffered
   // rows share the same reference basis as the already-stored ones.
   std::string local_reference;

   bool local_reference_contains_missing_symbol;

  public:
   // Decompresses 'sequenceCompressed' input during phase-1 extraction. Uses the
   // initial (unadapted) reference, which is what inputs are compressed against.
   ZstdDecompressor compressed_input_decompressor;

   SequenceColumnBuilder(
      const SequenceColumnMetadata<SymbolType>* metadata,
      std::string local_reference
   );

   // Diffs the sequence against the local reference and buffers the differences.
   // Throws AppendException on an illegal character or an over-long sequence.
   void insert(
      std::string_view sequence,
      uint32_t offset,
      const std::vector<std::string>& insertions
   );

   void insertNull() {
      buffer.push_back(typename SequenceColumn<SymbolType>::BufferedSequence{.is_null = true});
   }

   void moveRowTo(size_t index, SequenceColumnBuilder& destination) {
      destination.buffer.push_back(std::move(buffer.at(index)));
   }

   // The covered [start, end) of the buffered row, or nullopt for a null / fully-missing row.
   // Lets the repartitioning step read a row's coverage without consuming the buffer.
   [[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> coverageRangeAt(size_t index) const {
      const auto& buffered = buffer.at(index);
      if (buffered.is_null || buffered.coverage.start == buffered.coverage.end) {
         return std::nullopt;
      }
      return std::make_pair(buffered.coverage.start, buffered.coverage.end);
   }

   [[nodiscard]] size_t numValues() const { return buffer.size(); }

   [[nodiscard]] SequenceColumn<SymbolType>::Buffer finalize() {
      typename SequenceColumn<SymbolType>::Buffer result = std::move(buffer);
      buffer.clear();
      return result;
   }
};

}  // namespace rhydb::storage::column

template <>
class [[maybe_unused]] fmt::formatter<rhydb::storage::column::SequenceColumnInfo> {
  public:
   static constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
   [[maybe_unused]] static auto format(
      const rhydb::storage::column::SequenceColumnInfo& sequence_store_info,
      format_context& ctx
   ) -> decltype(ctx.out());
};

BOOST_SERIALIZATION_SPLIT_FREE(rhydb::storage::column::SequenceColumnMetadata<rhydb::AminoAcid>);
namespace boost::serialization {
template <class Archive>
[[maybe_unused]] void save(
   Archive& archive,
   const rhydb::storage::column::SequenceColumnMetadata<rhydb::AminoAcid>& object,
   [[maybe_unused]] const uint32_t version
) {
   archive & object.column_name;
   archive & object.reference_sequence;
}
}  // namespace boost::serialization

BOOST_SERIALIZATION_SPLIT_FREE(
   std::shared_ptr<rhydb::storage::column::SequenceColumnMetadata<rhydb::AminoAcid>>
);
namespace boost::serialization {
template <class Archive>
[[maybe_unused]] void load(
   Archive& archive,
   std::shared_ptr<rhydb::storage::column::SequenceColumnMetadata<rhydb::AminoAcid>>& object,
   [[maybe_unused]] const uint32_t version
) {
   std::string column_name;
   std::vector<rhydb::AminoAcid::Symbol> reference_sequence;
   archive & column_name;
   archive & reference_sequence;
   object = std::make_shared<rhydb::storage::column::SequenceColumnMetadata<rhydb::AminoAcid>>(
      std::move(column_name), std::move(reference_sequence)
   );
}
}  // namespace boost::serialization

BOOST_SERIALIZATION_SPLIT_FREE(rhydb::storage::column::SequenceColumnMetadata<rhydb::Nucleotide>);
namespace boost::serialization {
template <class Archive>
[[maybe_unused]] void save(
   Archive& archive,
   const rhydb::storage::column::SequenceColumnMetadata<rhydb::Nucleotide>& object,
   [[maybe_unused]] const uint32_t version
) {
   archive & object.column_name;
   archive & object.reference_sequence;
}
}  // namespace boost::serialization

BOOST_SERIALIZATION_SPLIT_FREE(
   std::shared_ptr<rhydb::storage::column::SequenceColumnMetadata<rhydb::Nucleotide>>
);
namespace boost::serialization {
template <class Archive>
[[maybe_unused]] void load(
   Archive& archive,
   std::shared_ptr<rhydb::storage::column::SequenceColumnMetadata<rhydb::Nucleotide>>& object,
   [[maybe_unused]] const uint32_t version
) {
   std::string column_name;
   std::vector<rhydb::Nucleotide::Symbol> reference_sequence;
   archive & column_name;
   archive & reference_sequence;
   object = std::make_shared<rhydb::storage::column::SequenceColumnMetadata<rhydb::Nucleotide>>(
      std::move(column_name), std::move(reference_sequence)
   );
}
}  // namespace boost::serialization
