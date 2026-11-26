#include "rhydb/storage/column/sequence_column.h"

#include <algorithm>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <boost/lexical_cast.hpp>
#include <roaring/roaring.hh>

#include "rhydb/append/append_exception.h"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/aligned_sequence.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/string_utils.h"
#include "rhydb/preprocessing/preprocessing.h"
#include "rhydb/preprocessing/preprocessing_exception.h"
#include "rhydb/storage/insertion_format_exception.h"
#include "rhydb/storage/sequence/horizontal_coverage_index.h"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

namespace rhydb::storage::column {

namespace {

constexpr std::string_view DELIMITER_INSERTION = ":";

template <typename SymbolType>
struct InsertionEntry {
   uint32_t position_idx;
   std::string insertion;
};

template <typename SymbolType>
InsertionEntry<SymbolType> parseInsertion(const std::string& value) {
   auto position_and_insertion = splitBy(value, DELIMITER_INSERTION);
   std::ranges::transform(
      position_and_insertion, position_and_insertion.begin(), [](const std::string& value) {
         return removeSymbol(value, '\"');
      }
   );
   if (position_and_insertion.size() == 2 && !position_and_insertion.at(1).empty()) {
      try {
         const auto position_signed = boost::lexical_cast<int64_t>(position_and_insertion[0]);
         if (position_signed < 0) {
            throw rhydb::storage::InsertionFormatException(
               "Failed to parse insertion: position must not be negative, got: '{}'", value
            );
         }
         const auto position = static_cast<uint32_t>(position_signed);
         const auto& insertion = position_and_insertion[1];
         if (insertion.empty()) {
            throw rhydb::storage::InsertionFormatException(
               "Failed to parse insertion due to invalid format. Expected position that is "
               "parsable as an integer, instead got: '{}'",
               value
            );
         }
         for (char character : insertion) {
            auto symbol = SymbolType::charToSymbol(character);
            if (symbol == std::nullopt) {
               throw InsertionFormatException(
                  fmt::format(
                     "Illegal {} character '{}' in insertion: {}",
                     SymbolType::SYMBOL_NAME_LOWER_CASE,
                     character,
                     value
                  )
               );
            }
         }
         return {.position_idx = position, .insertion = insertion};
      } catch (const boost::bad_lexical_cast& error) {
         throw rhydb::storage::InsertionFormatException(
            "Failed to parse insertion due to invalid format. Expected position that is parsable "
            "as "
            "an integer, instead got: '{}'",
            value
         );
      }
   }
   throw rhydb::storage::InsertionFormatException(
      "Failed to parse insertion due to invalid format. Expected two parts (position and non-empty "
      "insertion "
      "value), instead got: '{}'",
      value
   );
}
}  // namespace

template <typename SymbolType>
SequenceColumn<SymbolType>::SequenceColumn(SequenceColumnMetadata<SymbolType>* metadata)
    : metadata(metadata),
      genome_length(metadata->reference_sequence.size()) {
   RHYDB_ASSERT_GT(genome_length, 0ULL);
   column_reference = sequence::SequenceReference::create<SymbolType>(
      SymbolType::sequenceToString(metadata->reference_sequence)
   );
}

template <typename SymbolType>
std::expected<void, std::string> SequenceColumn<SymbolType>::appendChunk(const Buffer& buffer) {
   // Parse every insertion first, so that a malformed one leaves the column unchanged
   std::vector<std::pair<uint16_t, InsertionEntry<SymbolType>>> insertions;
   for (size_t row_in_chunk = 0; row_in_chunk < buffer.size(); ++row_in_chunk) {
      for (const auto& insertion : buffer[row_in_chunk].insertions.insertions) {
         auto parsed = parseInsertion<SymbolType>(insertion);
         if (parsed.position_idx > genome_length) {
            throw append::AppendException(
               "the insertion position ({}) is larger than the length of the reference sequence "
               "({})",
               parsed.position_idx,
               genome_length
            );
         }
         insertions.emplace_back(static_cast<uint16_t>(row_in_chunk), std::move(parsed));
      }
   }

   // Each ingestion chunk becomes its own `SequenceChunk`, whose rows are addressed by their index
   // in the chunk; the global row id of a row is `chunk_id << 16 | row_in_chunk` (see `RowId`). The
   // rows are buffered as differences from the column's reference; the chunk chooses its own local
   // reference from them, sharing an equal reference of the column or of a stored chunk.
   sequence::SequenceChunkBuilder builder{column_reference};
   for (const auto& buffered : buffer) {
      if (buffered.is_null) {
         builder.addNullRow();
      } else {
         builder.addRow(buffered.coverage, buffered.mutations);
      }
   }
   auto known_references = chunkReferences();
   known_references.push_back(column_reference);
   chunks_.push_back(
      std::make_shared<const sequence::SequenceChunk>(
         std::move(builder).finishWithLocalReference(known_references)
      )
   );
   sequence_count += static_cast<uint32_t>(buffer.size());

   for (const auto& [row_in_chunk, insertion] : insertions) {
      insertion_index.add(insertion.position_idx, insertion.insertion, row_in_chunk);
   }
   insertion_index.appendChunk();
   return {};
}

template <typename SymbolType>
std::vector<std::shared_ptr<const sequence::SequenceReference>> SequenceColumn<
   SymbolType>::chunkReferences() const {
   std::vector<std::shared_ptr<const sequence::SequenceReference>> references;
   for (const auto& chunk : chunks_) {
      if (std::ranges::find(references, chunk->sharedReference()) == references.end()) {
         references.push_back(chunk->sharedReference());
      }
   }
   return references;
}

template <typename SymbolType>
Bitmap SequenceColumn<SymbolType>::nullRows() const {
   return Bitmap::fromChunks(chunks_.size(), [&](size_t chunk_id) {
      return chunks_[chunk_id]->nullRows();
   });
}

template <typename SymbolType>
std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>> SequenceColumn<
   SymbolType>::storedRowsWithSymbolsAt(uint32_t position, sequence::SymbolSet symbols) const {
   std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>> result;
   for (size_t chunk_id = 0; chunk_id < chunks_.size(); ++chunk_id) {
      const auto& mutations = chunks_[chunk_id]->mutations();
      for (const auto& entry : mutations.entriesAtPosition(position)) {
         if (symbols.contains(entry.symbol)) {
            result.emplace_back(static_cast<uint16_t>(chunk_id), mutations.container(entry));
         }
      }
   }
   return result;
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::deleteRows(const RowCompaction& compaction) {
   std::vector<std::shared_ptr<const sequence::SequenceChunk>> remaining_chunks;
   for (size_t index = 0; index < chunks_.size(); ++index) {
      const auto chunk_id = static_cast<uint16_t>(index);
      if (!compaction.newChunkId(chunk_id).has_value()) {
         continue;
      }
      if (compaction.losesSomeRows(chunk_id)) {
         remaining_chunks.push_back(
            std::make_shared<const sequence::SequenceChunk>(
               chunks_[index]->compacted(compaction, chunk_id)
            )
         );
      } else {
         remaining_chunks.push_back(chunks_[index]);
      }
   }
   chunks_ = std::move(remaining_chunks);
   insertion_index.deleteRows(compaction);
   // Every row, null or not, counts as a sequence
   sequence_count = compaction.compactedLayout().numRows();
   calculateInfo();
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::finalize() {
   const SequenceColumnInfo info = calculateInfo();
   SPDLOG_DEBUG(
      "Sequence store info: {}, {} distinct local references", info, chunkReferences().size()
   );
}

template <typename SymbolType>
SequenceColumnInfo SequenceColumn<SymbolType>::calculateInfo() {
   size_t vertical_bitmaps_size = 0;
   size_t horizontal_bitmaps_size = 0;
   std::vector<uint32_t> positions;
   for (const auto& chunk : chunks_) {
      vertical_bitmaps_size += chunk->mutationsSizeInBytes();
      // Reported as the size of one run-optimized roaring bitmap of each row's missing positions,
      // as they were stored before the per-chunk indexes, so that the figure stays comparable
      chunk->coverage().forEachRowWithMissing(
         [&](uint16_t /*row*/, std::span<const sequence::MissingRange> ranges) {
            // Built from the positions like the bitmaps were, so the representation (and thus the
            // size) roaring picks is the same
            positions.clear();
            for (const auto& [begin, end] : ranges) {
               for (uint32_t position = begin; position < end; ++position) {
                  positions.push_back(position);
               }
            }
            roaring::Roaring bitmap;
            bitmap.addMany(positions.size(), positions.data());
            bitmap.runOptimize();
            bitmap.shrinkToFit();
            horizontal_bitmaps_size += bitmap.getSizeInBytes(false);
         }
      );
   }
   sequence_column_info = {
      .sequence_count = sequence_count,
      .vertical_bitmaps_size = vertical_bitmaps_size,
      .horizontal_bitmaps_size = horizontal_bitmaps_size,
   };
   return sequence_column_info;
}

template <typename SymbolType>
SequenceColumnInfo SequenceColumn<SymbolType>::getInfo() const {
   return sequence_column_info;
}

}  // namespace rhydb::storage::column

[[maybe_unused]] auto fmt::formatter<rhydb::storage::column::SequenceColumnInfo>::format(
   const rhydb::storage::column::SequenceColumnInfo& sequence_store_info,
   fmt::format_context& ctx
) -> decltype(ctx.out()) {
   return fmt::format_to(
      ctx.out(),
      "SequenceColumnInfo[sequence count: {}, vertical bitmaps size: {}, horizontal bitmaps size: "
      "{}]",
      sequence_store_info.sequence_count,
      sequence_store_info.vertical_bitmaps_size,
      sequence_store_info.horizontal_bitmaps_size
   );
}

namespace rhydb::storage::column {

template <typename SymbolType>
SequenceColumnMetadata<SymbolType>::SequenceColumnMetadata(
   std::string column_name,
   std::vector<typename SymbolType::Symbol>&& reference_sequence
)
    : ColumnMetadata(std::move(column_name)),
      reference_sequence(std::move(reference_sequence)) {}

template <typename SymbolType>
SequenceColumnBuilder<SymbolType>::SequenceColumnBuilder(
   const SequenceColumnMetadata<SymbolType>* metadata,
   std::string reference
)
    : reference(std::move(reference)),
      compressed_input_decompressor(
         std::make_shared<ZstdDDictionary>(
            SymbolType::sequenceToString(metadata->reference_sequence)
         )
      ) {
   RHYDB_ASSERT_GT(metadata->reference_sequence.size(), 0ULL);
   RHYDB_ASSERT_EQ(this->reference.size(), metadata->reference_sequence.size());
   reference_contains_missing_symbol = referenceContainsMissingSymbol<SymbolType>(this->reference);
}

template <typename SymbolType>
void SequenceColumnBuilder<SymbolType>::insert(
   std::string_view sequence,
   uint32_t offset,
   const std::vector<std::string>& insertions
) {
   const size_t genome_length = reference.size();
   if (sequence.size() + offset > genome_length) {
      throw append::AppendException(
         "the sequence '{}' which was inserted with an offset {} is larger than the length of "
         "the reference genome: {}",
         sequence,
         offset,
         genome_length
      );
   }
   RHYDB_ASSERT(sequence.size() + offset < UINT32_MAX);

   auto coverage_mutations = extractCoverageAndMutationsFromSequence<SymbolType>(
      sequence, offset, std::string_view{reference}, reference_contains_missing_symbol
   );
   if (!coverage_mutations.has_value()) {
      SPDLOG_INFO("{}", coverage_mutations.error());
      throw append::AppendException{coverage_mutations.error()};
   }
   auto [coverage, mutations] = std::move(coverage_mutations).value();

   buffer.push_back(
      typename SequenceColumn<SymbolType>::BufferedSequence{
         .coverage = std::move(coverage),
         .mutations = std::move(mutations),
         .insertions = {insertions},
      }
   );
}

template class SequenceColumn<Nucleotide>;
template class SequenceColumn<AminoAcid>;
template class SequenceColumnMetadata<Nucleotide>;
template class SequenceColumnMetadata<AminoAcid>;
template class SequenceColumnBuilder<Nucleotide>;
template class SequenceColumnBuilder<AminoAcid>;
}  // namespace rhydb::storage::column
