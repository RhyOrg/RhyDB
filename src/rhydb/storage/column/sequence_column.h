#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <span>
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
/// 2^16-row ingestion chunk (see documentation/developer/sequence_storage.md). All chunks are
/// stored against the column's local reference, which `finalize` adapts to the most common symbol
/// at each position, replacing the chunks with chunks stored against the adapted reference. The
/// insertions are held in a column-wide `InsertionIndex`.
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

   // The local reference is written once; the chunks are written without it and share it again
   // after loading. Their indexes are written as raw bytes, so loading rebuilds nothing.
   template <class Archive>
   void save(Archive& archive, [[maybe_unused]] const uint32_t version) const {
      const std::string& local_reference_text = local_reference->text();
      archive << local_reference_text;
      archive << insertion_index;
      archive << sequence_column_info;
      archive << sequence_count;
      const uint64_t num_chunks = chunks_.size();
      archive << num_chunks;
      for (const auto& chunk : chunks_) {
         chunk->save(archive);
      }
   }

   template <class Archive>
   void load(Archive& archive, [[maybe_unused]] const uint32_t version) {
      std::string local_reference_text;
      archive >> local_reference_text;
      local_reference =
         sequence::SequenceReference::create<SymbolType>(std::move(local_reference_text));
      archive >> insertion_index;
      archive >> sequence_column_info;
      archive >> sequence_count;
      uint64_t num_chunks = 0;
      archive >> num_chunks;
      chunks_.clear();
      chunks_.reserve(num_chunks);
      for (uint64_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
         chunks_.push_back(
            std::make_shared<const sequence::SequenceChunk>(
               sequence::SequenceChunk::load(archive, local_reference)
            )
         );
      }
   }

   BOOST_SERIALIZATION_SPLIT_MEMBER()

   /// The reference all chunks are stored against; shared with them.
   std::shared_ptr<const sequence::SequenceReference> local_reference;

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

   [[nodiscard]] const std::shared_ptr<const sequence::SequenceReference>& localReference() const {
      return local_reference;
   }

   /// The local reference as text; inserted sequences are diffed against it.
   [[nodiscard]] const std::string& localReferenceText() const { return local_reference->text(); }

   [[nodiscard]] std::vector<typename SymbolType::Symbol> getLocalReference() const {
      std::vector<typename SymbolType::Symbol> result;
      result.reserve(local_reference->length());
      for (uint32_t position = 0; position < local_reference->length(); ++position) {
         result.push_back(static_cast<SymbolType::Symbol>(local_reference->symbolAt(position)));
      }
      return result;
   }

   [[nodiscard]] SymbolType::Symbol getLocalReferencePosition(size_t position) const {
      RHYDB_ASSERT(position < metadata->reference_sequence.size());
      return static_cast<SymbolType::Symbol>(
         local_reference->symbolAt(static_cast<uint32_t>(position))
      );
   }

   [[nodiscard]] SequenceColumnInfo getInfo() const;

   /// Appends a finalized ingestion chunk as a new `SequenceChunk`, stored against the current
   /// local reference.
   std::expected<void, std::string> appendChunk(const Buffer& buffer);

   [[nodiscard]] bool isNull(RowId row_id) const {
      return chunks_[row_id.chunk_id]->isNull(row_id.row_in_chunk);
   }

   /// The null rows of all chunks, as views into the chunks.
   [[nodiscard]] Bitmap nullRows() const;

   /// The length of every (non-null) row's text: the length of the reference sequence.
   [[nodiscard]] size_t sequenceLength() const { return local_reference->length(); }

   /// Views of the rows that store one of `symbols` at `position`, per chunk, in ascending chunk
   /// order (see `storedSymbolsAt`). A chunk id appears once per stored symbol of `symbols`.
   [[nodiscard]] std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>>
   storedRowsWithSymbolsAt(uint32_t position, sequence::SymbolSet symbols) const;

   /// Removes the rows `compaction` deletes and renumbers the remaining ones, see `RowCompaction`.
   /// The chunks that lose some rows are rebuilt without them; the others are kept as they are.
   void deleteRows(const RowCompaction& compaction);

   /// Adapts the local reference to the most common symbol at each position and replaces the
   /// chunks with chunks stored against the adapted reference.
   void finalize();

  private:
   void adaptLocalReference();

   SequenceColumnInfo calculateInfo();
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
