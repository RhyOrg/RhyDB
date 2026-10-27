#include "rhydb/query_engine/operators/map_node.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/compute/api.h>
#include <arrow/datum.h>
#include <nlohmann/json_fwd.hpp>

#include "rhydb/common/size_constants.h"
#include "rhydb/query_engine/exec_node/batch_reslicer.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/concat.h"
#include "rhydb/query_engine/scalar_expressions/iso_week.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"

namespace rhydb::query_engine::operators {

using scalar_expressions::At;
using scalar_expressions::Concat;
using scalar_expressions::dynCast;
using scalar_expressions::IsoWeek;
using scalar_expressions::ScalarExpression;
using scalar_expressions::ZstdDecompressScalar;

namespace {

/// Sums the dictionary sizes of every `ZstdDecompressScalar` anywhere in `expression`'s tree. A
/// decompress may be nested inside another scalar expression (e.g. `At(ZstdDecompress(...))` after
/// a map merge), so the whole tree is traversed rather than just the top node.
// NOLINTNEXTLINE(misc-no-recursion)
size_t sumDecompressDictionarySizes(const ScalarExpression& expression) {
   if (const auto* zstd = dynCast<ZstdDecompressScalar>(&expression)) {
      return zstd->dictionary_string.size() + sumDecompressDictionarySizes(*zstd->input);
   }
   if (const auto* at_function = dynCast<At>(&expression)) {
      return sumDecompressDictionarySizes(*at_function->input);
   }
   if (const auto* iso_week = dynCast<IsoWeek>(&expression)) {
      return sumDecompressDictionarySizes(*iso_week->input);
   }
   if (const auto* concat = dynCast<Concat>(&expression)) {
      return sumDecompressDictionarySizes(*concat->left) +
             sumDecompressDictionarySizes(*concat->right);
   }
   return 0;
}

/// Estimates by how many bytes the assignments inflate each row, i.e. the summed dictionary sizes
/// of their zstd decompressions. Decompression inflates each value by (roughly) the size of its
/// dictionary (the reference sequence).
size_t estimateInflatedBytesPerRow(
   const std::map<std::string, const MapNode::Assignment*>& assignment_by_name
) {
   size_t inflated_bytes_per_row = 0;
   for (const auto& assignment : assignment_by_name | std::views::values) {
      inflated_bytes_per_row += sumDecompressDictionarySizes(*assignment->expression);
   }
   return inflated_bytes_per_row;
}

/// Insert a backpressure sink/source pair into the plan so that Arrow can throttle the upstream
/// appropriately before decompression. The source reslices its input into batches of at most
/// `maximum_batch_size` rows, for inputs that do not already honor the reduced
/// `materialization_cutoff` (see `MapNode::addToExecPlan`).
arrow::Result<arrow::acero::ExecNode*> insertBackpressureForDecompression(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* input_node,
   int64_t maximum_batch_size
) {
   const auto& input_ordering = input_node->ordering();

   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> batch_generator;
   arrow::acero::BackpressureMonitor* backpressure_monitor;
   std::shared_ptr<arrow::Schema> schema_of_sequence_batches;
   ARROW_ASSIGN_OR_RAISE(
      auto* current_node,
      arrow::acero::MakeExecNode(
         "sink",
         &plan,
         {input_node},
         arrow::acero::SinkNodeOptions{
            &batch_generator,
            &schema_of_sequence_batches,
            // Pause as soon as one batch is queued and resume only once it is taken. The queued
            // batches are still compressed, so a byte threshold would hold many times its size
            // in decompressed sequences, all of which the reslicer below emits in one burst.
            arrow::acero::BackpressureOptions{/*resume_if_below=*/1, /*pause_if_above=*/1},
            &backpressure_monitor,
         }
      )
   );
   current_node->SetLabel(
      "additional sink node to help backpressure application before zstd decompression"
   );

   ARROW_ASSIGN_OR_RAISE(
      current_node,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{
            schema_of_sequence_batches,
            rhydb::query_engine::exec_node::BatchReslicer{
               batch_generator,
               maximum_batch_size,
               backpressure_monitor,
            },
            input_ordering,
         }
      )
   );
   current_node->SetLabel(
      "additional source node to help backpressure application before zstd decompression"
   );

   return current_node;
}

}  // namespace

MapNode::MapNode(QueryNodePtr child, std::vector<Assignment> assignments)
    : child(std::move(child)),
      assignments(std::move(assignments)) {}

std::vector<schema::ColumnIdentifier> MapNode::getOutputSchema() const {
   auto output = child->getOutputSchema();
   for (const auto& assignment : assignments) {
      auto found = std::ranges::find_if(output, [&](const auto& column) {
         return column.name == assignment.output_column.name;
      });
      if (found != output.end()) {
         *found = assignment.output_column;
      } else {
         output.push_back(assignment.output_column);
      }
   }
   return output;
}

arrow::Result<arrow::acero::ExecNode*> MapNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options
) const {
   std::map<std::string, const Assignment*> assignment_by_name;
   for (const auto& assignment : assignments) {
      assignment_by_name[assignment.output_column.name] = &assignment;
   }

   const size_t inflated_bytes_per_row = estimateInflatedBytesPerRow(assignment_by_name);
   if (inflated_bytes_per_row == 0) {
      ARROW_ASSIGN_OR_RAISE(auto* current_node, child->addToExecPlan(plan, tables, query_options));
      return addProjection(plan, current_node, assignment_by_name);
   }

   // Restrict the batch size of the whole pipeline below, so that a batch takes up at most ~64MB
   // once inflated. `materialization_cutoff` is the batch size minus one.
   const size_t maximum_batch_size = std::max(common::S_64_MB / inflated_bytes_per_row, 1UL);
   config::QueryOptions child_query_options = query_options;
   child_query_options.materialization_cutoff =
      std::min(query_options.materialization_cutoff, maximum_batch_size - 1);

   ARROW_ASSIGN_OR_RAISE(
      auto* current_node, child->addToExecPlan(plan, tables, child_query_options)
   );
   if (std::getenv("RHYDB_EXP_NO_RESLICER") != nullptr) {  // EXPERIMENT
      return addProjection(plan, current_node, assignment_by_name);
   }
   ARROW_ASSIGN_OR_RAISE(
      current_node,
      insertBackpressureForDecompression(
         plan, current_node, static_cast<int64_t>(maximum_batch_size)
      )
   );
   return addProjection(plan, current_node, assignment_by_name);
}

arrow::Result<arrow::acero::ExecNode*> MapNode::addProjection(
   arrow::acero::ExecPlan& plan,
   arrow::acero::ExecNode* input_node,
   const std::map<std::string, const Assignment*>& assignment_by_name
) const {
   const auto output_schema = getOutputSchema();
   std::vector<arrow::Expression> expressions;
   std::vector<std::string> names;
   expressions.reserve(output_schema.size());
   names.reserve(output_schema.size());
   for (const auto& [name, type] : output_schema) {
      names.push_back(name);
      auto found = assignment_by_name.find(name);
      if (found == assignment_by_name.end()) {
         // Column passed through unchanged from the child.
         expressions.push_back(arrow::compute::field_ref(name));
         continue;
      }
      ARROW_ASSIGN_OR_RAISE(auto arrow_expression, found->second->expression->toArrowExpression());
      expressions.push_back(std::move(arrow_expression));
   }

   const arrow::acero::ProjectNodeOptions options{std::move(expressions), std::move(names)};
   return arrow::acero::MakeExecNode("project", &plan, {input_node}, options);
}

nlohmann::json MapNode::toJson() const {
   nlohmann::json map_expressions = nlohmann::json::array();
   for (const auto& [field, expression] : assignments) {
      map_expressions.push_back(nlohmann::json{field.name, expression->toString()});
   }
   return nlohmann::json{
      {"type", nodeKindToString(kind())},
      {"child", child->toJson()},
      {"mapExpressions", map_expressions},
   };
}

}  // namespace rhydb::query_engine::operators
