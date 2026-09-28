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
      position_and_insertion,
      position_and_insertion.begin(),
      [](const std::string& value) { return removeSymbol(value, '\"'); }
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
               throw InsertionFormatException(fmt::format(
                  "Illegal {} character '{}' in insertion: {}",
                  SymbolType::SYMBOL_NAME_LOWER_CASE,
                  character,
                  value
               ));
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
      genome_length(metadata->reference_sequence.size()),
      local_reference_sequence_string(SymbolType::sequenceToString(metadata->reference_sequence)) {
   mutation_buffer.resize(genome_length);
   RHYDB_ASSERT_GT(genome_length, 0ULL);
}

template <typename SymbolType>
std::expected<void, std::string> SequenceColumn<SymbolType>::appendChunk(const Buffer& buffer) {
   // Each ingestion chunk occupies its own 2^16-aligned block of the global row-id space: chunk `k`
   // spans `[k << 16, (k << 16) + buffer.size())` (see `RowId`). Assigning sparse, chunk-aligned
   // ids here keeps the sequence column's indexes consistent with the value columns and the shared
   // `RowLayout`, which is what the query path addresses rows by.
   for (size_t row_in_chunk = 0; row_in_chunk < buffer.size(); ++row_in_chunk) {
      const RowId row_id(num_chunks, row_in_chunk);
      const auto& buffered = buffer[row_in_chunk];
      if (buffered.is_null) {
         appendNullValue(row_id);
      } else {
         appendValue(row_id, buffered);
      }
   }
   num_chunks++;
   // The vertical index keys its roaring containers by the row id's high 16 bits (= the chunk id),
   // so a chunk's mutations never share a container with another chunk's. Flushing the per-position
   // mutation buffer at every chunk boundary therefore both keeps it bounded and is correct.
   flushBuffer();
   return {};
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::appendValue(RowId row_id, const BufferedSequence& buffered) {
   sequence_count++;

   horizontal_coverage_index.insertCoverage(row_id, buffered.coverage);

   for (const auto& [position_idx, symbol] : buffered.mutations.mutations) {
      mutation_buffer.at(position_idx)[symbol].push_back(row_id.toGlobal());
   }

   for (const auto& insertion_and_position : buffered.insertions.insertions) {
      auto [position, insertion] = parseInsertion<SymbolType>(insertion_and_position);
      if (position > genome_length) {
         throw append::AppendException(
            "the insertion position ({}) is larger than the length of the reference sequence ({})",
            position,
            genome_length
         );
      }
      insertion_index.addLazily(position, insertion, row_id.toGlobal());
   }
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::appendNullValue(RowId row_id) {
   null_bitmap.add(row_id.toGlobal());
   sequence_count += 1;
   horizontal_coverage_index.insertNullSequence(row_id);
}

template <typename SymbolType>
bool SequenceColumn<SymbolType>::isNull(RowId row_id) const {
   return null_bitmap.contains(row_id.toGlobal());
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::finalize() {
   flushBuffer();

   SPDLOG_DEBUG("Building insertion index");
   insertion_index.buildIndex();

   const SequenceColumnInfo info_after_filling = calculateInfo();

   SPDLOG_DEBUG("Adapting local reference");
   // We only need the number of rows covering a position to decide reference symbol adaptation
   const std::vector<uint64_t> coverage_cardinalities =
      horizontal_coverage_index.computeCoverageCardinalities(genome_length);
   for (uint32_t position_idx = 0; position_idx < genome_length; ++position_idx) {
      const auto current_reference_symbol =
         SymbolType::charToSymbol(local_reference_sequence_string.at(position_idx)).value();
      if (!vertical_sequence_index
              .findBetterLocalReferenceSymbol(
                 position_idx, current_reference_symbol, coverage_cardinalities.at(position_idx)
              )
              .has_value()) {
         continue;
      }
      // Only now compute the coverage bitmap for the position that needs to be adapted
      const auto coverage_bitmaps =
         horizontal_coverage_index.getCoverageBitmapForPositions<1>(position_idx);
      const roaring::Roaring& coverage_bitmap = coverage_bitmaps[0];
      const auto new_reference_symbol = vertical_sequence_index.adaptLocalReference(
         coverage_bitmap, position_idx, current_reference_symbol
      );
      RHYDB_ASSERT(new_reference_symbol.has_value());
      SPDLOG_DEBUG(
         "At position {} adapted local reference symbol to '{}'",
         position_idx,
         SymbolType::symbolToChar(new_reference_symbol.value())
      );
      local_reference_sequence_string.at(position_idx) =
         SymbolType::symbolToChar(new_reference_symbol.value());
   }

   const SequenceColumnInfo info_after_adaption = calculateInfo();

   SPDLOG_DEBUG("Optimizing bitmaps");

   optimizeBitmaps();

   const SequenceColumnInfo info_after_optimisation = calculateInfo();

   SPDLOG_DEBUG(
      "Sequence store info after filling it: {}, after local reference adaption: {}, and "
      "after optimising: {}",
      info_after_filling,
      info_after_adaption,
      info_after_optimisation
   );
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
SequenceColumnInfo SequenceColumn<SymbolType>::calculateInfo() {
   sequence_column_info = {
      .sequence_count = sequence_count,
      .vertical_bitmaps_size = computeVerticalBitmapsSize(),
      .horizontal_bitmaps_size = computeHorizontalBitmapsSize(),
   };
   return sequence_column_info;
}

template <typename SymbolType>
SequenceColumnInfo SequenceColumn<SymbolType>::getInfo() const {
   return sequence_column_info;
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::fillIndexes() {
   for (size_t position_idx = 0; position_idx != genome_length; ++position_idx) {
      vertical_sequence_index.addSymbolsToPositions(position_idx, mutation_buffer.at(position_idx));
   }
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::optimizeBitmaps() {
   for (auto& [sequence_diff_key, sequence_diff] : vertical_sequence_index.vertical_bitmaps) {
      sequence_diff.runOptimizeAndShrink();
   }
}

template <typename SymbolType>
void SequenceColumn<SymbolType>::flushBuffer() {
   fillIndexes();
   for (auto& position : mutation_buffer) {
      for (auto symbol : SymbolType::SYMBOLS) {
         position[symbol].clear();
      }
   }
}

template <typename SymbolType>
size_t SequenceColumn<SymbolType>::computeVerticalBitmapsSize() const {
   size_t result = 0;
   for (const auto& [_pos, sequence_diff] : vertical_sequence_index.vertical_bitmaps) {
      result += sequence_diff.sizeInBytes();
   }
   return result;
}

template <typename SymbolType>
size_t SequenceColumn<SymbolType>::computeHorizontalBitmapsSize() const {
   size_t result = 0;
   for (const auto& [_pos, bitmap] : horizontal_coverage_index.horizontal_bitmaps) {
      result += bitmap.getSizeInBytes(false);
   }
   return result;
}

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
   std::string local_reference
)
    : local_reference(std::move(local_reference)),
      compressed_input_decompressor(std::make_shared<ZstdDDictionary>(
         SymbolType::sequenceToString(metadata->reference_sequence)
      )) {
   RHYDB_ASSERT_GT(metadata->reference_sequence.size(), 0ULL);
   RHYDB_ASSERT_EQ(this->local_reference.size(), metadata->reference_sequence.size());
   local_reference_contains_missing_symbol =
      referenceContainsMissingSymbol<SymbolType>(this->local_reference);
}

template <typename SymbolType>
void SequenceColumnBuilder<SymbolType>::insert(
   std::string_view sequence,
   uint32_t offset,
   const std::vector<std::string>& insertions
) {
   const size_t genome_length = local_reference.size();
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
      sequence, offset, std::string_view{local_reference}, local_reference_contains_missing_symbol
   );
   if (!coverage_mutations.has_value()) {
      SPDLOG_INFO("{}", coverage_mutations.error());
      throw append::AppendException{coverage_mutations.error()};
   }
   auto [coverage, mutations] = std::move(coverage_mutations).value();

   buffer.push_back(typename SequenceColumn<SymbolType>::BufferedSequence{
      .coverage = std::move(coverage), .mutations = std::move(mutations), .insertions = {insertions},
   });
}

template class SequenceColumn<Nucleotide>;
template class SequenceColumn<AminoAcid>;
template class SequenceColumnMetadata<Nucleotide>;
template class SequenceColumnMetadata<AminoAcid>;
template class SequenceColumnBuilder<Nucleotide>;
template class SequenceColumnBuilder<AminoAcid>;
}  // namespace rhydb::storage::column
