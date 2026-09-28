#include "rhydb/append/table_inserter.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "evobench/evobench.hpp"
#include "rhydb/append/append_exception.h"
#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/error.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/storage/column/sequence_column.h"

namespace rhydb::append {

namespace {

/// The sequence column whose coverage drives clustered routing, looked up by name. Throws if no
/// name was given or it does not name a sequence column of the table.
schema::ColumnIdentifier resolveDriverColumn(
   const schema::TableSchema& schema,
   const std::optional<std::string>& driver_column_name
) {
   if (!driver_column_name.has_value()) {
      throw AppendException("clustered ingestion requires a driver column name");
   }
   const auto column = schema.getColumn(*driver_column_name);
   if (!column.has_value()) {
      throw AppendException(
         "the clustering driver column '{}' does not exist in the table schema", *driver_column_name
      );
   }
   if (column.value().type != schema::ColumnType::NUCLEOTIDE_SEQUENCE &&
       column.value().type != schema::ColumnType::AMINO_ACID_SEQUENCE) {
      throw AppendException(
         "the clustering driver column '{}' must be a nucleotide or amino-acid sequence column",
         *driver_column_name
      );
   }
   return *column;
}

size_t genomeLengthOf(const storage::Table& table, const schema::ColumnIdentifier& driver_column) {
   if (driver_column.type == schema::ColumnType::NUCLEOTIDE_SEQUENCE) {
      return table.columns.getColumns<storage::column::SequenceColumn<Nucleotide>>()
         .at(driver_column.name)
         .genome_length;
   }
   return table.columns.getColumns<storage::column::SequenceColumn<AminoAcid>>()
      .at(driver_column.name)
      .genome_length;
}

std::expected<simdjson::ondemand::value, std::string> findFieldManual(
   simdjson::ondemand::object& object,
   const schema::ColumnIdentifier& column_identifier
) {
   object.reset();
   for (auto maybe_field : object) {
      ASSIGN_OR_RAISE_SIMDJSON(
         simdjson::ondemand::field,
         field,
         std::move(maybe_field),
         "Could not parse field from object"
      );
      ASSIGN_OR_RAISE_SIMDJSON(
         std::string_view,
         unescaped_key,
         field.unescaped_key(),
         "Could not unescape the key {}. Probably a utf-8 error",
         field.escaped_key()
      );
      if (unescaped_key == column_identifier.name) {
         static std::once_flag warn_once;
         std::call_once(warn_once, [&]() {
            SPDLOG_WARN(
               "The key '{}' which requires unescaping does not use the same unescaping in the "
               "current line ('{}') as in the first line of the ndjson file. This leads to worse "
               "performance during parsing",
               unescaped_key,
               field.escaped_key()
            );
         });
         return field.value();
      }
   }
   return std::unexpected(
      fmt::format("Did not find the field '{}' in the given json", column_identifier.name)
   );
}

std::expected<simdjson::ondemand::value, std::string> findFieldWithFallbacks(
   simdjson::ondemand::object& object,
   const TableInserter::SniffedField& sniffed_field
) {
   simdjson::ondemand::value column_value;
   auto error = object.find_field(sniffed_field.escaped_key).get(column_value);
   if (!error) {
      return column_value;
   }
   error = object.find_field_unordered(sniffed_field.escaped_key).get(column_value);
   if (!error) {
      static std::once_flag warn_once;
      std::call_once(warn_once, [&]() {
         SPDLOG_WARN(
            "The key '{}' was ordered differently in the current line than in the first line of "
            "the file. "
            "There might be an error in the data generation. "
            "The performance is better if the fields are ordered regularly.",
            sniffed_field.escaped_key
         );
      });
      return column_value;
   }
   // Even for fields that require escaping, we get the escaped value from the sniffed field, so
   // this should never occur in well-formatted ndjson files. Print a warning in findFieldManual
   return findFieldManual(object, sniffed_field.column_identifier);
}

std::expected<simdjson::ondemand::object, std::string> iterateToObject(
   simdjson::ondemand::document_reference ndjson_line
) {
   simdjson::ondemand::object object;
   if (auto error = ndjson_line.get_object().get(object)) {
      if (error == simdjson::INCOMPLETE_ARRAY_OR_OBJECT) {
         return std::unexpected(
            "the ndjson line does not contain valid json (incomplete object or array)"
         );
      }
      if (error == simdjson::INCORRECT_TYPE) {
         simdjson::ondemand::json_type type;
         error = ndjson_line.type().get(type);
         if (!error) {
            // type does not offer a toString-like method, but only this stream-method
            std::stringstream error_message;
            error_message << "expect each ndjson line to be an object, got type '" << type << "'";
            return std::unexpected(error_message.str());
         }
      }
      return std::unexpected(
         fmt::format("unexpected error {} while parsing line", simdjson::error_message(error))
      );
   }
   return object;
}

}  // namespace

TableInserter::TableInserter(
   std::shared_ptr<storage::Table> table,
   ClusteredBufferingOptions options
)
    : table(std::move(table)),
      driver_column(
         options.enabled
            ? std::optional{resolveDriverColumn(*this->table->schema, options.driver_column_name)}
            : std::nullopt
      ),
      input_buffer{*this->table->schema, this->table->columns},
      null_buffer{
         storage::ColumnGroupBuilder{*this->table->schema, this->table->columns},
         std::nullopt
      } {
   if (driver_column.has_value()) {
      const size_t genome_length = genomeLengthOf(*this->table, *driver_column);
      growth_threshold = static_cast<uint32_t>(
         std::llround(options.span_growth_threshold_fraction * static_cast<double>(genome_length))
      );
      const size_t num_buffers = std::max<size_t>(1, options.num_buffers);
      output_buffers.reserve(num_buffers);
      for (size_t i = 0; i < num_buffers; ++i) {
         output_buffers.push_back(ClusterBuffer{
            storage::ColumnGroupBuilder{*this->table->schema, this->table->columns}, std::nullopt,
         });
      }
   }
}

TableInserter::ClusterBuffer& TableInserter::chooseBuffer(
   const std::optional<std::pair<uint32_t, uint32_t>>& row_range
) {
   // Null / fully-missing driver sequences carry no coverage information; they get their own buffer
   // so they never widen a data buffer's range.
   if (!row_range.has_value()) {
      return null_buffer;
   }
   const auto [row_start, row_end] = *row_range;

   ClusterBuffer* best = nullptr;
   uint32_t best_growth = 0;
   ClusterBuffer* free_slot = nullptr;
   for (auto& buffer : output_buffers) {
      if (!buffer.range.has_value()) {
         if (free_slot == nullptr) {
            free_slot = &buffer;
         }
         continue;
      }
      const auto [start, end] = *buffer.range;
      // How much adding this row would widen the buffer's covered span. Zero when the row is
      // already contained in the buffer's range.
      const uint32_t merged_span = std::max(end, row_end) - std::min(start, row_start);
      const uint32_t growth = merged_span - (end - start);
      if (best == nullptr || growth < best_growth) {
         best = &buffer;
         best_growth = growth;
      }
   }

   // Join the least-growth buffer if it stays tight enough, or if there is no free slot to open a
   // new cluster. Otherwise seed a fresh buffer with this row's range.
   if (best != nullptr && (best_growth <= growth_threshold || free_slot == nullptr)) {
      best->range = {
         std::min(best->range->first, row_start), std::max(best->range->second, row_end),
      };
      return *best;
   }
   RHYDB_ASSERT(free_slot != nullptr);
   free_slot->range = std::make_pair(row_start, row_end);
   return *free_slot;
}

std::expected<std::vector<TableInserter::SniffedField>, std::string> TableInserter::sniffFieldOrder(
   simdjson::ondemand::document_reference ndjson_line
) const {
   std::vector<SniffedField> order_in_json_line;
   auto columns_in_table = table->columns.metadata;
   ASSIGN_OR_RAISE(auto object, iterateToObject(ndjson_line));
   for (auto maybe_field : object) {
      ASSIGN_OR_RAISE_SIMDJSON(
         simdjson::ondemand::field,
         field,
         std::move(maybe_field),
         "error '{}', while parsing a field of the json object"
      );

      std::string_view raw_key_sv = field.escaped_key();

      ASSIGN_OR_RAISE_SIMDJSON(
         std::string_view,
         unescaped_key,
         field.unescaped_key(),
         "error '{1}', when unescaping the key '{0}'",
         raw_key_sv
      );

      auto maybe_column_metadata = std::ranges::find_if(
         columns_in_table, [&](const auto& identifier) { return identifier.name == unescaped_key; }
      );
      if (maybe_column_metadata == columns_in_table.end()) {
         SPDLOG_WARN(
            "The field '{}' which is contained in the input json file is not in the database. "
            "Ignoring field.",
            unescaped_key
         );
         continue;
      }
      order_in_json_line.push_back(SniffedField{
         .column_identifier = *maybe_column_metadata, .escaped_key = std::string{raw_key_sv},
      });
   }
   for (const auto& column_metadata : columns_in_table) {
      const bool contained_in_sniffed_fields =
         std::ranges::find_if(order_in_json_line, [&](const auto& sniffed_field) {
            return sniffed_field.column_identifier.name == column_metadata.name;
         }) != order_in_json_line.end();
      if (!contained_in_sniffed_fields) {
         return std::unexpected{
            fmt::format("the column '{}' is not contained in the object", column_metadata.name),
         };
      }
   }
   ndjson_line.rewind();
   return order_in_json_line;
}

std::expected<void, std::string> TableInserter::flushIfFull(ClusterBuffer& buffer) {
   if (buffer.builder.numBufferedRows() >= storage::column::COLUMN_CHUNK_SIZE) {
      auto bulk_insert_result = table->bulkInsert(buffer.builder);
      if (!bulk_insert_result.has_value()) {
         return bulk_insert_result;
      }
      buffer.range = std::nullopt;
   }
   return {};
}

std::expected<void, std::string> TableInserter::flushInputBuffer() {
   const size_t num_rows = input_buffer.numBufferedRows();
   if (num_rows == 0) {
      return {};
   }
   if (!clusteringEnabled()) {
      // Append the whole input buffer as a single chunk; bulkInsert consumes (clears) it.
      return table->bulkInsert(input_buffer);
   }
   // Streaming partition: assign each buffered row to an output buffer by its driver coverage and
   // move it there, flushing any output buffer that fills. Per-row assignment, no sorting.
   for (size_t row = 0; row < num_rows; ++row) {
      const auto driver_range = input_buffer.coverageRangeAt(*driver_column, row);
      ClusterBuffer& target = chooseBuffer(driver_range);
      input_buffer.moveRowTo(row, target.builder);
      auto flush_result = flushIfFull(target);
      if (!flush_result.has_value()) {
         return flush_result;
      }
   }
   // The rows have been moved out; drop the leftover moved-from slots.
   input_buffer.clear();
   return {};
}

std::expected<void, std::string> TableInserter::insert(
   simdjson::ondemand::document_reference ndjson_line,
   const std::vector<SniffedField>& field_order_hint
) {
   EVOBENCH_SCOPE_EVERY(20, "TableInserter", "insert");
   ASSIGN_OR_RAISE(auto object, iterateToObject(ndjson_line));
   for (const auto& sniffed_field : field_order_hint) {
      ASSIGN_OR_RAISE(auto column_value, findFieldWithFallbacks(object, sniffed_field));
      auto success_or_error =
         input_buffer.addJsonValueToColumn(sniffed_field.column_identifier, column_value);
      if (!success_or_error.has_value()) {
         return success_or_error;
      }
   }
   if (input_buffer.numBufferedRows() >= storage::column::COLUMN_CHUNK_SIZE) {
      return flushInputBuffer();
   }
   return {};
}

TableInserter::Commit TableInserter::commit() {
   const auto throw_on_error = [](const std::expected<void, std::string>& result) {
      if (!result.has_value()) {
         throw AppendException(result.error());
      }
   };
   // Partition the final partial input batch, then flush every partially-filled output buffer as
   // its own chunk. Order does not matter: each flush appends an independent chunk.
   throw_on_error(flushInputBuffer());
   const auto flush_buffer = [&](ClusterBuffer& buffer) {
      if (buffer.builder.numBufferedRows() > 0) {
         throw_on_error(table->bulkInsert(buffer.builder));
      }
   };
   flush_buffer(null_buffer);
   for (auto& buffer : output_buffers) {
      flush_buffer(buffer);
   }
   table->finalize();
   table->validate();
   return Commit{};
}

void NdjsonInsertStream::insertAll(NdjsonLineReader& input_data) {
   for (auto [json_obj_or_error, raw_line] : input_data) {
      simdjson::ondemand::document_reference ndjson_line;
      if (auto error = json_obj_or_error.get(ndjson_line)) {
         throw AppendException(
            "get error '{}' when parsing the current line: {}",
            simdjson::error_message(error),
            raw_line
         );
      }

      if (!field_order_sniffed) {
         auto sniffed_field_order_or_error = table_inserter->sniffFieldOrder(ndjson_line);
         if (!sniffed_field_order_or_error.has_value()) {
            throw AppendException{
               "{} - current line: {}", sniffed_field_order_or_error.error(), raw_line,
            };
         }
         field_order = sniffed_field_order_or_error.value();
         field_order_sniffed = true;
      }

      auto maybe_error = table_inserter->insert(ndjson_line, field_order);
      if (!maybe_error.has_value()) {
         throw AppendException{"{} - current line: {}", maybe_error.error(), raw_line};
      }

      line_count++;
      if (line_count % 10000 == 0) {
         SPDLOG_INFO("Processed {} json objects from the input file", line_count);
      }
   }
}

TableInserter::Commit appendDataToTable(
   std::shared_ptr<storage::Table> table,
   NdjsonLineReader& input_data,
   ClusteredBufferingOptions options
) {
   EVOBENCH_SCOPE("TableInserter", "appendDataToTable");
   TableInserter table_inserter(std::move(table), std::move(options));

   NdjsonInsertStream insert_stream{table_inserter};
   insert_stream.insertAll(input_data);

   return table_inserter.commit();
}

}  // namespace rhydb::append
