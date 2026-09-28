#include "rhydb/query_engine/exec_node/zstd_decompress_expression.h"

#include <gtest/gtest.h>

#include <utility>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/acero/query_context.h>
#include <arrow/api.h>
#include <arrow/compute/api.h>
#include <arrow/compute/expression.h>
#include <arrow/compute/kernel.h>
#include <arrow/compute/registry.h>
#include <arrow/memory_pool.h>
#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/type.h>
#include <arrow/type_traits.h>
#include <spdlog/spdlog.h>

#include "rhydb/zstd/zstd_compressor.h"
#include "rhydb/zstd/zstd_dictionary.h"

namespace {
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<std::shared_ptr<arrow::Table>> setupTestTable(
   std::vector<std::optional<std::string>> values,
   std::string_view dictionary_string
) {
   std::shared_ptr<arrow::Schema> schema = arrow::schema(
      {arrow::field("id", arrow::int32()),
       arrow::field("some_zstd_compressed_column", arrow::binary()),}
   );

   auto dictionary = std::make_shared<rhydb::ZstdCDictionary>(dictionary_string, 3);
   rhydb::ZstdCompressor compressor{dictionary};

   arrow::Int32Builder id_builder;
   arrow::BinaryBuilder value_builder;
   int32_t id_column_value = 1;
   for (auto& value : values) {
      ARROW_RETURN_NOT_OK(id_builder.Append(id_column_value++));
      if (value.has_value()) {
         ARROW_RETURN_NOT_OK(value_builder.Append(compressor.compress(value->data(), value->size()))
         );
      } else {
         ARROW_RETURN_NOT_OK(value_builder.AppendNull());
      }
   }
   ARROW_ASSIGN_OR_RAISE(std::shared_ptr<arrow::Array> id_array, id_builder.Finish());
   ARROW_ASSIGN_OR_RAISE(std::shared_ptr<arrow::Array> value_array, value_builder.Finish());
   return arrow::Table::Make(schema, {id_array, value_array});
}

using rhydb::query_engine::exec_node::ZstdDecompressExpression;
std::shared_ptr<arrow::Table> runValuesThroughProjection(
   std::vector<std::optional<std::string>> values,
   const std::string& dictionary_string
) {
   auto input_table = setupTestTable(std::move(values), dictionary_string).ValueOrDie();

   auto arrow_plan = arrow::acero::ExecPlan::Make().ValueOrDie();

   arrow::acero::TableSourceNodeOptions source_options{input_table};
   auto* node =
      arrow::acero::MakeExecNode("table_source", arrow_plan.get(), {}, source_options).ValueOrDie();

   arrow::acero::ProjectNodeOptions project_options(
      {arrow::compute::field_ref("id"),
       ZstdDecompressExpression::make(
          arrow::compute::field_ref("some_zstd_compressed_column"), dictionary_string
       ),}
   );
   node =
      arrow::acero::MakeExecNode("project", arrow_plan.get(), {node}, project_options).ValueOrDie();

   std::shared_ptr<arrow::Table> result_table;

   arrow::acero::TableSinkNodeOptions options{&result_table};
   arrow::acero::MakeExecNode("table_sink", arrow_plan.get(), {node}, options).ValueOrDie();

   arrow_plan->StartProducing();
   arrow_plan->finished().result().ValueOrDie();

   return result_table;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void assertDecompressedStringArray(
   const std::vector<std::optional<std::string>>& expected_values,
   const std::shared_ptr<arrow::Table>& result_table
) {
   for (int chunk_id = 0; chunk_id < result_table->column(1)->num_chunks(); ++chunk_id) {
      std::shared_ptr<arrow::StringArray> string_array =
         std::static_pointer_cast<arrow::StringArray>(result_table->column(1)->chunk(0));

      ASSERT_EQ(string_array->length(), expected_values.size())
         << "Decompressed array length does not match expected values size.";

      for (size_t i = 0; i < expected_values.size(); i++) {
         if (string_array->IsNull(i)) {
            ASSERT_FALSE(expected_values[i].has_value())
               << "Value at index " << i << " is null, but expected a value.";
         } else {
            ASSERT_TRUE(expected_values[i].has_value())
               << "Value at index " << i << " is not null, but expected null.";
            ASSERT_EQ(string_array->Value(i), expected_values[i].value())
               << "Decompressed value at index " << i << " does not match expected.";
         }
      }
   }
}
}  // namespace

TEST(ZstdDecompressExpression, decompressesValues) {
   std::vector<std::optional<std::string>> values = {"ACGT", "ACCT", "ACGG"};
   auto result_table = runValuesThroughProjection(values, "ACGTC");
   assertDecompressedStringArray(values, result_table);
}

TEST(ZstdDecompressExpression, worksWithNullValues) {
   std::vector<std::optional<std::string>> values = {std::nullopt, "ACCT", std::nullopt};
   auto result_table = runValuesThroughProjection(values, "ACGTC");
   assertDecompressedStringArray(values, result_table);
}

TEST(ZstdDecompressExpression, empty) {
   std::vector<std::optional<std::string>> values = {};
   auto result_table = runValuesThroughProjection(values, "ACGTC");
   assertDecompressedStringArray(values, result_table);
}

TEST(ZstdDecompressExpression, largeSet) {
   std::vector<std::optional<std::string>> values = {};
   values.reserve(25002);
   for (size_t i = 0; i < 15000; ++i) {
      values.emplace_back("ACGT");
   }
   values.emplace_back(std::nullopt);
   for (size_t i = 0; i < 10000; ++i) {
      values.emplace_back("AAAA");
   }
   auto result_table = runValuesThroughProjection(values, "ACGTC");
   assertDecompressedStringArray(values, result_table);
}
