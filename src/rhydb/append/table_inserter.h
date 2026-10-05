#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <simdjson.h>
#include <spdlog/spdlog.h>

#include "rhydb/append/ndjson_line_reader.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"
#include "rhydb/storage/table_chunk_builder.h"

namespace rhydb::append {

/// Controls the N-way clustered buffering during ingestion. When enabled, rows are routed to one of
/// `num_buffers` open buffers by the covered range of a single driver sequence column, so that each
/// flushed chunk holds sequences of similar coverage (which compresses better).
///
/// Off by default: ingestion then stays single-buffer and strictly in input order, byte-identical
/// to the pre-clustering behavior. This matters because clustering reorders rows across chunks,
/// which changes the row iteration order that order-sensitive query results depend on. Callers
/// (and, once wired, config) must opt in explicitly.
struct ClusteredBufferingOptions {
   /// Master switch. False -> single-buffer, in-order ingestion (old behavior); the fields below
   /// are ignored.
   bool enabled = false;
   /// Name of the sequence column whose coverage drives routing. Required when enabled; must name a
   /// sequence column of the table.
   std::optional<std::string> driver_column_name;
   /// Number of simultaneously open data buffers (>=1). A separate buffer holds null-driver rows.
   size_t num_buffers = 8;
   /// A row joins an existing buffer when doing so widens that buffer's covered span by at most
   /// this fraction of the genome length; otherwise it opens a new buffer (until num_buffers is
   /// reached).
   double span_growth_threshold_fraction = 0.10;
};

class TableInserter {
   std::shared_ptr<storage::Table> table;

   /// One output buffer plus its current bounding covered range over the driver column.
   /// `range == nullopt` marks an empty slot (also true right after a flush).
   struct ClusterBuffer {
      storage::TableChunkBuilder builder;
      std::optional<std::pair<uint32_t, uint32_t>> range;
   };

   /// The driver sequence column, or nullopt when clustering is disabled.
   std::optional<schema::ColumnIdentifier> driver_column;
   /// Max span increase (in genome positions) still allowed to merge a row into an existing buffer.
   uint32_t growth_threshold = 0;
   /// The reader inserts every row into this single buffer. When it fills (COLUMN_CHUNK_SIZE rows)
   /// it is flushed: appended directly as one chunk (clustering off) or repartitioned across the
   /// output buffers by driver coverage (clustering on).
   storage::TableChunkBuilder input_buffer;
   /// Per-cluster output buffers. Each accumulates coverage-homogeneous rows across successive
   /// input batches and flushes independently once full. Empty when clustering is disabled.
   std::vector<ClusterBuffer> output_buffers;
   /// Receives rows whose driver sequence is null or fully missing (unused when clustering
   /// disabled).
   ClusterBuffer null_buffer;

   [[nodiscard]] bool clusteringEnabled() const { return driver_column.has_value(); }

   /// Flush the full input buffer: append it directly (clustering off), or repartition its rows
   /// across the output buffers by driver coverage (clustering on). Resets the input buffer.
   [[nodiscard]] std::expected<void, std::string> flushInputBuffer();

   /// Pick (and widen the range of) the output buffer a row with the given driver coverage joins.
   ClusterBuffer& chooseBuffer(const std::optional<std::pair<uint32_t, uint32_t>>& row_range);

   /// Flush the buffer as a chunk (via bulkInsert) once it reaches COLUMN_CHUNK_SIZE rows, freeing
   /// its slot for a new cluster.
   [[nodiscard]] std::expected<void, std::string> flushIfFull(ClusterBuffer& buffer);

  public:
   class Commit {
      friend class TableInserter;

      Commit() = default;
   };

   explicit TableInserter(
      std::shared_ptr<storage::Table> table,
      ClusteredBufferingOptions options = {}
   );

   struct SniffedField {
      rhydb::schema::ColumnIdentifier column_identifier;
      // Looking up keys in their escaped form is fastest.
      // As fallback in case we do not find the key in the escaped form
      // in subsequent json elements, will unescape the key.
      std::string escaped_key;
   };

   // Inserting is faster if we parse the fields in the correct order.
   // Sniff the order from the first json in the ndjson stream
   [[nodiscard]] std::expected<std::vector<SniffedField>, std::string> sniffFieldOrder(
      simdjson::ondemand::document_reference ndjson_line
   ) const;

   [[nodiscard]] std::expected<void, std::string> insert(
      simdjson::ondemand::document_reference ndjson_line,
      const std::vector<SniffedField>& field_order_hint
   );

   [[nodiscard]] Commit commit();
};

/// Feeds NDJSON lines into a TableInserter. It keeps the field order sniffed from the very first
/// line and the running line count across calls, so a stream that arrives in several chunks (e.g.
/// one query result batch at a time) is inserted exactly as one continuous NDJSON stream would be.
/// Throws AppendException for a line that cannot be parsed or inserted.
class NdjsonInsertStream {
   TableInserter* table_inserter;
   std::vector<TableInserter::SniffedField> field_order;
   bool field_order_sniffed = false;
   size_t line_count = 0;

  public:
   explicit NdjsonInsertStream(TableInserter& table_inserter)
       : table_inserter(&table_inserter) {}

   /// Inserts every line that `input_data` yields.
   void insertAll(NdjsonLineReader& input_data);

   [[nodiscard]] size_t lineCount() const { return line_count; }
};

TableInserter::Commit appendDataToTable(
   std::shared_ptr<rhydb::storage::Table> table,
   NdjsonLineReader& input_data,
   ClusteredBufferingOptions options = {}
);

}  // namespace rhydb::append
