#include "rhydb/query_engine/exec_node/string_to_bool_expression.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/api.h>
#include <arrow/compute/expression.h>

namespace {

using rhydb::query_engine::exec_node::StringToBoolExpression;

arrow::Result<std::shared_ptr<arrow::Table>> runThroughProjection(
   const std::vector<std::optional<std::string>>& values
) {
   arrow::LargeStringBuilder builder;
   for (const auto& value : values) {
      if (value.has_value()) {
         ARROW_RETURN_NOT_OK(builder.Append(*value));
      } else {
         ARROW_RETURN_NOT_OK(builder.AppendNull());
      }
   }
   ARROW_ASSIGN_OR_RAISE(auto array, builder.Finish());
   auto input_table =
      arrow::Table::Make(arrow::schema({arrow::field("s", arrow::large_utf8())}), {array});

   ARROW_ASSIGN_OR_RAISE(auto plan, arrow::acero::ExecPlan::Make());
   ARROW_ASSIGN_OR_RAISE(
      auto* node,
      arrow::acero::MakeExecNode(
         "table_source", plan.get(), {}, arrow::acero::TableSourceNodeOptions{input_table}
      )
   );
   ARROW_ASSIGN_OR_RAISE(
      node,
      arrow::acero::MakeExecNode(
         "project",
         plan.get(),
         {node},
         arrow::acero::ProjectNodeOptions(
            {StringToBoolExpression::make(arrow::compute::field_ref("s"))}
         )
      )
   );
   std::shared_ptr<arrow::Table> result_table;
   ARROW_RETURN_NOT_OK(
      arrow::acero::MakeExecNode(
         "table_sink", plan.get(), {node}, arrow::acero::TableSinkNodeOptions{&result_table}
      )
         .status()
   );
   plan->StartProducing();
   ARROW_RETURN_NOT_OK(plan->finished().status());
   return result_table;
}

}  // namespace

TEST(StringToBoolExpression, parsesTrueFalseAndNull) {
   const auto table = runThroughProjection({"true", "false", std::nullopt, "true"}).ValueOrDie();
   const auto column = table->column(0)->chunk(0);
   const auto& booleans = static_cast<const arrow::BooleanArray&>(*column);

   ASSERT_EQ(booleans.length(), 4);
   EXPECT_TRUE(booleans.Value(0));
   EXPECT_FALSE(booleans.Value(1));
   EXPECT_TRUE(booleans.IsNull(2));
   EXPECT_TRUE(booleans.Value(3));
}

TEST(StringToBoolExpression, handlesEmptyInput) {
   const auto table = runThroughProjection({}).ValueOrDie();
   EXPECT_EQ(table->num_rows(), 0);
}

TEST(StringToBoolExpression, rejectsEverythingButExactTrueAndFalse) {
   for (const std::string invalid : {"TRUE", "False", "1", "0", "t", "yes", " true", "true ", ""}) {
      const auto result = runThroughProjection({"true", invalid});
      ASSERT_FALSE(result.ok()) << "'" << invalid << "' was accepted";
      EXPECT_NE(result.status().message().find("expected 'true' or 'false'"), std::string::npos)
         << result.status().ToString();
   }
}
