#include "rhydb/append/table_inserter.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "rhydb/append/ndjson_line_reader.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::Nucleotide;
using rhydb::append::appendDataToTable;
using rhydb::append::ClusteredBufferingOptions;
using rhydb::append::NdjsonLineReader;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
using rhydb::schema::TableName;
using rhydb::schema::TableSchema;
using rhydb::storage::Table;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::SequenceColumn;
using rhydb::storage::column::SequenceColumnMetadata;
using rhydb::storage::column::StringColumnMetadata;

namespace {

constexpr std::string_view PK_COLUMN = "pk";
constexpr std::string_view SEQ_COLUMN = "seq";

// A table with a string primary key and a single nucleotide sequence column whose reference is
// `genome_length` copies of 'A'.
std::shared_ptr<Table> makeTable(size_t genome_length) {
   const ColumnIdentifier pk_id{.name = std::string{PK_COLUMN}, .type = ColumnType::STRING};
   const ColumnIdentifier seq_id{
      .name = std::string{SEQ_COLUMN}, .type = ColumnType::NUCLEOTIDE_SEQUENCE,
   };
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata;
   column_metadata.emplace(pk_id, std::make_shared<StringColumnMetadata>(std::string{PK_COLUMN}));
   std::vector<Nucleotide::Symbol> reference(genome_length, Nucleotide::Symbol::A);
   column_metadata.emplace(
      seq_id,
      std::make_shared<SequenceColumnMetadata<Nucleotide>>(
         std::string{SEQ_COLUMN}, std::move(reference)
      )
   );
   auto schema = std::make_shared<TableSchema>(std::move(column_metadata), pk_id);
   return std::make_shared<Table>(TableName::getDefault(), schema);
}

// A full-length sequence covering [start, end): 'A' (matching the reference) inside the range and
// 'N' (missing) outside it, so its extracted coverage is exactly [start, end).
std::string coveredSequence(size_t genome_length, uint32_t start, uint32_t end) {
   std::string sequence(genome_length, 'N');
   for (uint32_t position = start; position < end; ++position) {
      sequence[position] = 'A';
   }
   return sequence;
}

// One ndjson row. covered == nullopt emits a null sequence.
std::string row(std::string_view primary_key, std::optional<std::string> covered_sequence) {
   if (!covered_sequence.has_value()) {
      return fmt::format(R"({{"{}":"{}","{}":null}})", PK_COLUMN, primary_key, SEQ_COLUMN) + "\n";
   }
   return fmt::format(
             R"({{"{}":"{}","{}":{{"sequence":"{}","insertions":[]}}}})",
             PK_COLUMN,
             primary_key,
             SEQ_COLUMN,
             *covered_sequence
          ) +
          "\n";
}

ClusteredBufferingOptions clusteringOn(size_t num_buffers, double threshold_fraction = 0.10) {
   return ClusteredBufferingOptions{
      .enabled = true,
      .driver_column_name = std::string{SEQ_COLUMN},
      .num_buffers = num_buffers,
      .span_growth_threshold_fraction = threshold_fraction,
   };
}

void appendRows(
   const std::shared_ptr<Table>& table,
   const std::string& ndjson,
   ClusteredBufferingOptions options
) {
   std::stringstream input{ndjson};
   NdjsonLineReader reader{input};
   appendDataToTable(table, reader, std::move(options));
}

const rhydb::storage::column::HorizontalCoverageIndex& coverageIndex(const Table& table) {
   return table.columns.getColumns<SequenceColumn<Nucleotide>>()
      .at(std::string{SEQ_COLUMN})
      .horizontal_coverage_index;
}

// All rows' covered [start, end) ranges across every chunk, flattened.
std::vector<std::pair<uint32_t, uint32_t>> allRowRanges(const Table& table) {
   const auto& index = coverageIndex(table);
   std::vector<std::pair<uint32_t, uint32_t>> ranges;
   for (size_t chunk_id = 0; chunk_id < index.starts.size(); ++chunk_id) {
      const auto& chunk_starts = index.starts[chunk_id];
      const auto& chunk_ends = index.ends[chunk_id];
      for (size_t row_in_chunk = 0; row_in_chunk < chunk_starts.size(); ++row_in_chunk) {
         ranges.emplace_back(chunk_starts[row_in_chunk], chunk_ends[row_in_chunk]);
      }
   }
   return ranges;
}

}  // namespace

TEST(ClusteredBuffering, disjointRangesBeyondThresholdOpenSeparateChunks) {
   auto table = makeTable(100);
   const std::string ndjson =
      row("a", coveredSequence(100, 0, 20)) + row("b", coveredSequence(100, 80, 100));

   appendRows(table, ndjson, clusteringOn(8));

   EXPECT_EQ(table->row_layout.numChunks(), 2);
}

TEST(ClusteredBuffering, overlappingRangesWithinThresholdShareOneChunk) {
   auto table = makeTable(100);
   const std::string ndjson =
      row("a", coveredSequence(100, 0, 20)) + row("b", coveredSequence(100, 0, 25));

   appendRows(table, ndjson, clusteringOn(8));

   EXPECT_EQ(table->row_layout.numChunks(), 1);
}

TEST(ClusteredBuffering, respectsBufferCapacityByMergingIntoLeastGrowthBuffer) {
   auto table = makeTable(100);
   // Three mutually distant clusters but only two buffers: the third must merge into an existing
   // buffer rather than open a third.
   const std::string ndjson = row("a", coveredSequence(100, 0, 10)) +
                              row("b", coveredSequence(100, 45, 55)) +
                              row("c", coveredSequence(100, 90, 100));

   appendRows(table, ndjson, clusteringOn(2));

   EXPECT_EQ(table->row_layout.numChunks(), 2);
}

TEST(ClusteredBuffering, nullSequencesClusterSeparatelyFromDataRows) {
   auto table = makeTable(100);
   const std::string ndjson = row("a", coveredSequence(100, 0, 20)) + row("n1", std::nullopt) +
                              row("n2", std::nullopt) + row("b", coveredSequence(100, 0, 25));

   appendRows(table, ndjson, clusteringOn(8));

   // One chunk for the two data rows, one for the two null rows.
   EXPECT_EQ(table->row_layout.numChunks(), 2);
   const auto& index = coverageIndex(*table);
   const auto& nuc_column =
      table->columns.getColumns<SequenceColumn<Nucleotide>>().at(std::string{SEQ_COLUMN});
   EXPECT_EQ(nuc_column.null_bitmap.cardinality(), 2);

   // The data chunk's bounding range is exactly [0,25) — the null rows never widened it.
   bool found_data_batch = false;
   for (size_t chunk_id = 0; chunk_id < index.batch_min_start.size(); ++chunk_id) {
      if (index.batch_min_start[chunk_id] == 0 && index.batch_max_end[chunk_id] == 25) {
         found_data_batch = true;
      }
   }
   EXPECT_TRUE(found_data_batch);
}

TEST(ClusteredBuffering, flushesBufferWhenItReachesChunkSize) {
   auto table = makeTable(1);
   constexpr size_t CHUNK_SIZE = rhydb::storage::column::COLUMN_CHUNK_SIZE;
   std::string ndjson;
   ndjson.reserve((CHUNK_SIZE + 1) * 40);
   for (size_t i = 0; i < CHUNK_SIZE + 1; ++i) {
      ndjson += row(fmt::format("row-{}", i), coveredSequence(1, 0, 1));
   }

   appendRows(table, ndjson, clusteringOn(8));

   // A full first chunk was flushed mid-stream; the overflow row lands in a second chunk.
   EXPECT_EQ(table->row_layout.numChunks(), 2);
   EXPECT_EQ(table->sequence_count, CHUNK_SIZE + 1);
}

TEST(ClusteredBuffering, preservesAllRowsRegardlessOfChunkAssignment) {
   const std::string ndjson = row("a", coveredSequence(60, 0, 60)) +
                              row("b", coveredSequence(60, 0, 30)) + row("c", std::nullopt) +
                              row("d", coveredSequence(60, 40, 60)) +
                              row("e", coveredSequence(60, 0, 58));

   auto clustered = makeTable(60);
   appendRows(clustered, ndjson, clusteringOn(8));

   auto baseline = makeTable(60);
   appendRows(baseline, ndjson, ClusteredBufferingOptions{});  // clustering off

   EXPECT_EQ(clustered->sequence_count, baseline->sequence_count);

   const auto& clustered_nuc =
      clustered->columns.getColumns<SequenceColumn<Nucleotide>>().at(std::string{SEQ_COLUMN});
   const auto& baseline_nuc =
      baseline->columns.getColumns<SequenceColumn<Nucleotide>>().at(std::string{SEQ_COLUMN});
   EXPECT_EQ(clustered_nuc.null_bitmap.cardinality(), baseline_nuc.null_bitmap.cardinality());

   // Clustering only reorders rows across chunks, so the multiset of covered ranges is invariant.
   auto clustered_ranges = allRowRanges(*clustered);
   auto baseline_ranges = allRowRanges(*baseline);
   std::ranges::sort(clustered_ranges);
   std::ranges::sort(baseline_ranges);
   EXPECT_EQ(clustered_ranges, baseline_ranges);
}

TEST(ClusteredBuffering, disabledByDefaultKeepsSingleInOrderBuffer) {
   auto table = makeTable(100);
   // Two disjoint ranges that would open separate chunks if clustering were on.
   const std::string ndjson =
      row("a", coveredSequence(100, 0, 20)) + row("b", coveredSequence(100, 80, 100));

   appendRows(table, ndjson, ClusteredBufferingOptions{});

   EXPECT_EQ(table->row_layout.numChunks(), 1);
}
