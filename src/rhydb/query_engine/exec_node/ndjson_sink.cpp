#include "rhydb/query_engine/exec_node/ndjson_sink.h"

#include <ios>
#include <string_view>

#include <arrow/acero/options.h>
#include <arrow/array.h>
#include <arrow/array/array_binary.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/common/date32.h"
#include "rhydb/common/panic.h"

namespace rhydb::query_engine::exec_node {

namespace {

void writeChunked(std::ostream& output, std::string_view content) {
   const size_t chunk_size = 8192;
   for (size_t pos = 0; pos < content.size(); pos += chunk_size) {
      const size_t remaining_size = content.size() - pos;
      const size_t write_size = std::min(chunk_size, remaining_size);
      output.write(content.data() + pos, static_cast<std::streamsize>(write_size));
      output.flush();  // Flush after each small chunk
   }
}

template <size_t BatchSize>
struct BatchedStringStream {
   std::array<std::stringstream, BatchSize> streams;

   void operator<<(std::string_view bytes) {
      for (size_t i = 0; i < BatchSize; ++i) {
         streams[i] << bytes;
      }
   }

   void operator>>(std::ostream& output) {
      for (size_t i = 0; i < BatchSize; ++i) {
         const std::string content = std::move(streams[i]).str();
         writeChunked(output, content);
      }
   }
};

template <size_t BatchSize>
class ArrayToJsonTypeVisitor : public arrow::ArrayVisitor {
   BatchedStringStream<BatchSize>& output_stream;
   size_t& row_base;

  public:
   ArrayToJsonTypeVisitor(BatchedStringStream<BatchSize>& output_stream, size_t& row_base)
       : output_stream(output_stream),
         row_base(row_base) {}

   arrow::Status Visit(const arrow::Int32Array& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            output_stream.streams.at(i) << array.GetView(row_base + i);
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::Int64Array& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            output_stream.streams.at(i) << array.GetView(row_base + i);
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::DoubleArray& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            const nlohmann::json json = array.GetView(row_base + i);
            output_stream.streams.at(i) << json;
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::FloatArray& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            const nlohmann::json json = array.GetView(row_base + i);
            output_stream.streams.at(i) << json;
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::Date32Array& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            const std::string date_string = common::date32ToString(array.Value(row_base + i));
            const nlohmann::json json_string = date_string;
            output_stream.streams.at(i) << json_string.dump();
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::StringArray& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            const std::string_view value = array.GetView(row_base + i);
            const nlohmann::json json_string = value;
            output_stream.streams.at(i) << json_string.dump();
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status Visit(const arrow::BooleanArray& array) override {
      for (size_t i = 0; i < BatchSize; ++i) {
         if (array.IsNull(row_base + i)) {
            output_stream.streams.at(i) << "null";
         } else {
            output_stream.streams.at(i) << (array.GetView(row_base + i) ? "true" : "false");
         }
      }
      return arrow::Status::OK();
   }
};

template <size_t BatchSize>
void sendJsonLinesInBatches(
   size_t& row_idx_base,
   size_t row_count,
   const std::vector<std::string>& prepared_column_strings_for_json_attributes,
   const std::vector<std::shared_ptr<arrow::Array>>& column_arrays,
   std::ostream& output_stream
) {
   BatchedStringStream<BatchSize> ndjson_line_streams;
   ArrayToJsonTypeVisitor<BatchSize> my_visitor(ndjson_line_streams, row_idx_base);
   const size_t column_count = column_arrays.size();
   for (; row_idx_base + BatchSize <= row_count; row_idx_base += BatchSize) {
      ndjson_line_streams << "{";
      for (size_t column_idx = 0; column_idx < column_count; column_idx++) {
         const auto& column_array = column_arrays.at(column_idx);
         ndjson_line_streams << prepared_column_strings_for_json_attributes.at(column_idx);

         (void)column_array->Accept(&my_visitor);
      }
      ndjson_line_streams << "}\n";
      {
         EVOBENCH_SCOPE_EVERY(100, "QueryPlan", "sendDataToOutputStream");
         ndjson_line_streams >> output_stream;
      }
   }
   if constexpr (BatchSize > 1) {
      // Send remaining lines
      sendJsonLinesInBatches<1>(
         row_idx_base,
         row_count,
         prepared_column_strings_for_json_attributes,
         column_arrays,
         output_stream
      );
   }
}

}  // namespace

arrow::Status NdjsonSink::writeBatch(const arrow::compute::ExecBatch& batch) {
   EVOBENCH_SCOPE("QueryPlan", "writeBatchAsNdjson");
   const size_t row_count = batch.length;
   std::vector<std::shared_ptr<arrow::Array>> column_arrays;
   for (const auto& datum : batch.values) {
      RHYDB_ASSERT(datum.is_array() || datum.is_scalar());
      if (datum.is_array()) {
         column_arrays.emplace_back(datum.make_array());
      } else {
         ARROW_ASSIGN_OR_RAISE(auto array, arrow::MakeArrayFromScalar(*datum.scalar(), row_count));
         column_arrays.emplace_back(array);
      }
   }
   std::vector<std::string> prepared_column_strings_for_json_attributes;
   bool first_column = true;
   for (const auto& column_name : schema->fields()) {
      const nlohmann::json column_name_json = column_name->name();
      std::string json_formatted_column_name;
      if (!first_column) {
         json_formatted_column_name += ',';
      }
      first_column = false;
      json_formatted_column_name += column_name_json.dump();
      json_formatted_column_name += ':';
      prepared_column_strings_for_json_attributes.emplace_back(json_formatted_column_name);
   }
   size_t row_idx = 0;
   constexpr size_t BATCH_SIZE = 8;
   sendJsonLinesInBatches<BATCH_SIZE>(
      row_idx, row_count, prepared_column_strings_for_json_attributes, column_arrays, *output_stream
   );
   if (!*output_stream) {
      return arrow::Status::IOError("Could not write to network stream");
   }
   return arrow::Status::OK();
}

arrow::Status NdjsonSink::finish() {
   // TODO(#480) mark that the download is complete
   return arrow::Status::OK();
}

}  // namespace rhydb::query_engine::exec_node
