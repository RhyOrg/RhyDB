#include "rhydb/query_engine/command/write_command.h"

#include <sstream>

#include <arrow/api.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/exec_node/ndjson_sink.h"

using rhydb::query_engine::command::makeWriteSummary;
using rhydb::query_engine::command::writeToSink;

TEST(WriteCommand, makeWriteSummaryBuildsASingleRowTable) {
   const auto summary =
      makeWriteSummary("insertedRows", arrow::MakeScalar(int64_t{42})).ValueOrDie();

   ASSERT_EQ(summary->num_rows(), 1);
   ASSERT_EQ(summary->num_columns(), 1);
   EXPECT_EQ(summary->schema()->field(0)->name(), "insertedRows");
   EXPECT_TRUE(summary->schema()->field(0)->type()->Equals(arrow::int64()));
}

TEST(WriteCommand, writeToSinkWritesEveryBatchOfTheResult) {
   arrow::Int64Builder first_builder;
   ASSERT_TRUE(first_builder.AppendValues({1, 2}).ok());
   arrow::Int64Builder second_builder;
   ASSERT_TRUE(second_builder.AppendValues({3}).ok());
   const auto schema = arrow::schema({arrow::field("value", arrow::int64())});
   const auto chunked_array = std::make_shared<arrow::ChunkedArray>(
      arrow::ArrayVector{first_builder.Finish().ValueOrDie(), second_builder.Finish().ValueOrDie()}
   );
   const auto table = arrow::Table::Make(schema, {chunked_array});

   std::stringstream output;
   rhydb::query_engine::exec_node::NdjsonSink output_sink{&output, schema};
   ASSERT_TRUE(writeToSink(*table, output_sink).ok());

   EXPECT_EQ(output.str(), "{\"value\":1}\n{\"value\":2}\n{\"value\":3}\n");
}
