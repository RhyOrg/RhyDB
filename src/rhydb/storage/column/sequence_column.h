#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <boost/serialization/access.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/insertion_index.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/sequence/sequence_chunk.h"
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

/// A column of nucleotide or amino acid sequences, stored as one immutable `SequenceChunk` per
/// 2^16-row ingestion chunk (see documentation/developer/sequence_storage.md). Every chunk is
/// stored against its own local reference, chosen from the chunk's rows when it is appended, so a
/// stored chunk is never touched by later appends. Chunks with equal local references share one
/// `SequenceReference`. The insertions are held in a column-wide `InsertionIndex`.
template <typename SymbolType>
class SequenceColumn {
  public:
   using Metadata = SequenceColumnMetadata<SymbolType>;
   using Builder = SequenceColumnBuilder<SymbolType>;

   /// Compact, reference-diffed representation of a single sequence row. The
   /// builder diffs each inserted sequence against the column's reference
   /// sequence at insert time, so only the differences -- the
   /// covered range, the missing positions within it, and the mutations -- are
   /// buffered for the chunk. The full sequence is never retained, keeping
   /// per-chunk memory bounded regardless of genome length. appendChunk builds the
   /// chunk's `SequenceChunk` from this.
   struct BufferedSequence {
      bool is_null = false;
      Coverage coverage;
      Mutations<SymbolType> mutations;
      Insertions insertions;
   };
   using Buffer = std::vector<BufferedSequence>;

   static constexpr schema::DataType::Kind KIND = SymbolType::COLUMN_KIND;
   /// Has the length of the reference sequence, so there is no static `TYPE`
   [[nodiscard]] schema::DataTypePtr type() const {
      return schema::SequenceType::get(KIND, static_cast<uint32_t>(genome_length));
   }
   using value_type = void;

  private:
   friend class boost::serialization::access;

   // The distinct local references of the chunks are written once each, as their differences
   // from the column's reference; every chunk is written without its reference, preceded by the
   // index of its reference, so the loaded chunks share their references as before. The indexes
   // are written as raw bytes, so loading rebuilds nothing.
   template <class Archive>
   void save(Archive& archive, [[maybe_unused]] const uint32_t version) const {
      const auto references = chunkReferences();
      const uint64_t num_references = references.size();
      archive << num_references;
      for (const auto& reference : references) {
         const auto differences = reference->differencesFrom(*column_reference);
         const uint64_t num_differences = differences.size();
         archive << num_differences;
         for (const auto& [position, symbol] : differences) {
            archive << position;
            archive << symbol;
         }
      }
      archive << insertion_index;
      archive << sequence_column_info;
      archive << sequence_count;
      const uint64_t num_chunks = chunks_.size();
      archive << num_chunks;
      for (const auto& chunk : chunks_) {
         const auto reference_index = static_cast<uint32_t>(
            std::ranges::find(references, chunk->sharedReference()) - references.begin()
         );
         archive << reference_index;
         chunk->save(archive);
      }
   }

   /// Reads a count written as `uint64_t`, which a 32-bit process (wasm32) may not be able to hold.
   template <class Archive>
   static size_t loadCount(Archive& archive) {
      uint64_t count = 0;
      archive >> count;
      if (!std::in_range<size_t>(count)) {
         throw std::runtime_error(
            "cannot load a count of " + std::to_string(count) + " in this address space"
         );
      }
      return static_cast<size_t>(count);
   }

   template <class Archive>
   void load(Archive& archive, [[maybe_unused]] const uint32_t version) {
      const size_t num_references = loadCount(archive);
      std::vector<std::shared_ptr<const sequence::SequenceReference>> references;
      references.reserve(num_references);
      for (size_t index = 0; index < num_references; ++index) {
         const size_t num_differences = loadCount(archive);
         if (num_differences == 0) {
            references.push_back(column_reference);
            continue;
         }
         std::vector<sequence::SequenceReference::SymbolAt> differences(num_differences);
         for (auto& [position, symbol] : differences) {
            archive >> position;
            archive >> symbol;
         }
         references.push_back(column_reference->withSymbolsReplaced(differences));
      }
      archive >> insertion_index;
      archive >> sequence_column_info;
      archive >> sequence_count;
      const size_t num_chunks = loadCount(archive);
      chunks_.clear();
      chunks_.reserve(num_chunks);
      for (size_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
         uint32_t reference_index = 0;
         archive >> reference_index;
         chunks_.push_back(
            std::make_shared<const sequence::SequenceChunk>(
               sequence::SequenceChunk::load(archive, references.at(reference_index))
            )
         );
      }
   }

   BOOST_SERIALIZATION_SPLIT_MEMBER()

   /// The column's reference sequence (of the metadata), which inserted sequences are diffed
   /// against. The chunks whose local reference equals it share it.
   std::shared_ptr<const sequence::SequenceReference> column_reference;

   /// The chunks, indexed by chunk id. Shared with whoever reads them, so a
   /// chunk is never modified, only replaced.
   std::vector<std::shared_ptr<const sequence::SequenceChunk>> chunks_;

  public:
   const SequenceColumnMetadata<SymbolType>* metadata;
   const size_t genome_length;

   insertion::InsertionIndex<SymbolType> insertion_index;
   SequenceColumnInfo sequence_column_info;
   uint32_t sequence_count = 0;

   explicit SequenceColumn(Metadata* metadata);

   [[nodiscard]] size_t numChunks() const { return chunks_.size(); }

   /// Number of rows in ingestion chunk `chunk_id`. The shared `RowLayout` is the authoritative
   /// source, but the sequence column's chunks know their sizes too (every row, null or not, is a
   /// row of its chunk), so it satisfies the `Column` concept.
   [[nodiscard]] uint32_t chunkSize(uint16_t chunk_id) const {
      return chunks_.at(chunk_id)->numRows();
   }

   [[nodiscard]] const std::vector<std::shared_ptr<const sequence::SequenceChunk>>& chunks() const {
      return chunks_;
   }

   [[nodiscard]] const sequence::SequenceChunk& chunk(uint16_t chunk_id) const {
      return *chunks_[chunk_id];
   }

   /// The column's reference sequence (of the metadata).
   [[nodiscard]] const std::shared_ptr<const sequence::SequenceReference>& reference() const {
      return column_reference;
   }

   /// The column's reference sequence as text; inserted sequences are diffed against it.
   [[nodiscard]] std::string referenceText() const { return column_reference->text(); }

   /// The distinct local references of the chunks, in order of their first chunk.
   [[nodiscard]] std::vector<std::shared_ptr<const sequence::SequenceReference>>
   chunkReferences() const;

   [[nodiscard]] SequenceColumnInfo getInfo() const;

   /// Appends a finalized ingestion chunk as a new `SequenceChunk`, stored against the local
   /// reference chosen from its rows (`SequenceChunkBuilder::finishWithLocalReference`). The
   /// stored chunks are left as they are.
   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   [[nodiscard]] bool isNull(RowId row_id) const {
      return chunks_[row_id.chunk_id]->isNull(row_id.row_in_chunk);
   }

   /// The null rows of all chunks, as views into the chunks.
   [[nodiscard]] Bitmap nullRows() const;

   /// The length of every (non-null) row's text: the length of the reference sequence.
   [[nodiscard]] size_t sequenceLength() const { return column_reference->length(); }

   /// Views of the rows that store one of `symbols` at `position`, per chunk, in ascending chunk
   /// order (see `storedSymbolsAt`). A chunk id appears once per stored symbol of `symbols`.
   [[nodiscard]] std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>>
   storedRowsWithSymbolsAt(uint32_t position, sequence::SymbolSet symbols) const;

   /// Removes the rows `compaction` deletes and renumbers the remaining ones, see `RowCompaction`.
   /// The chunks that lose some rows are rebuilt without them; the others are kept as they are.
   void deleteRows(const RowCompaction& compaction);

   /// Updates the column info (`getInfo`). The chunks are left as they are.
   void finalize();

  private:
   SequenceColumnInfo calculateInfo();
};

/// Buffers one ingestion chunk by diffing each inserted sequence against the
/// column's reference sequence and storing only the differences (covered range,
/// missing positions, mutations). The full sequences are never retained, so
/// per-chunk memory stays bounded regardless of genome length.
template <typename SymbolType>
class SequenceColumnBuilder {
   SequenceColumn<SymbolType>::Buffer buffer;

   // The column's reference sequence, which inserted sequences are diffed against
   std::string reference;

   bool reference_contains_missing_symbol;

  public:
   // Decompresses 'sequenceCompressed' input during phase-1 extraction. Uses the
   // reference sequence, which is what inputs are compressed against.
   ZstdDecompressor compressed_input_decompressor;

   SequenceColumnBuilder(const SequenceColumnMetadata<SymbolType>* metadata, std::string reference);

   // Diffs the sequence against the reference and buffers the differences.
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
