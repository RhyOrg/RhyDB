#include "rhydb/query_engine/operators/map_node.h"

#include <algorithm>
#include <chrono>
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
#include "rhydb/query_engine/exec_node/throttled_batch_reslicer.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/iso_week.h"
#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"

namespace rhydb::query_engine::operators {

using scalar_expressions::At;
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
   return 0;
}

/// When any assignment uses zstd decompression, insert a backpressure sink/source pair into the
/// plan so that Arrow can throttle the upstream scan appropriately. Decompression inflates each
/// batch by (roughly) the reference/dictionary size, so we size the batches relative to the
/// summed reference sizes to bound peak memory. Output names are unique (handleMap rejects
/// duplicates).
///
/// Returns the new top node when a backpressure pair was inserted, or std::nullopt when no
/// assignment decompresses (in which case the caller keeps its existing node).
arrow::Result<std::optional<arrow::acero::ExecNode*>> insertBackpressureForDecompression(
   arrow::acero::ExecPlan& plan,
   const std::map<std::string, const MapNode::Assignment*>& assignment_by_name,
   arrow::acero::ExecNode* input_node
) {
   size_t sum_of_reference_genome_sizes = 0;
   for (const auto& assignment : assignment_by_name | std::views::values) {
      sum_of_reference_genome_sizes += sumDecompressDictionarySizes(*assignment->expression);
   }

   if (sum_of_reference_genome_sizes == 0) {
      return std::nullopt;
   }

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
            arrow::acero::BackpressureOptions{rhydb::common::S_16_KB, rhydb::common::S_64_MB},
            &backpressure_monitor,
         }
      )
   );
   current_node->SetLabel(
      "additional sink node to help backpressure application before zstd decompression"
   );

   const auto maximum_batch_size =
      static_cast<int64_t>(std::max(rhydb::common::S_64_MB / sum_of_reference_genome_sizes, 1UL));

   constexpr std::chrono::milliseconds TARGET_BATCH_RATE{667};

   ARROW_ASSIGN_OR_RAISE(
      current_node,
      arrow::acero::MakeExecNode(
         "source",
         &plan,
         {},
         arrow::acero::SourceNodeOptions{
            schema_of_sequence_batches,
            rhydb::query_engine::exec_node::ThrottledBatchReslicer{
               batch_generator, maximum_batch_size, TARGET_BATCH_RATE, backpressure_monitor,
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
   ARROW_ASSIGN_OR_RAISE(auto* current_node, child->addToExecPlan(plan, tables, query_options));

   std::map<std::string, const Assignment*> assignment_by_name;
   for (const auto& assignment : assignments) {
      assignment_by_name[assignment.output_column.name] = &assignment;
   }

   ARROW_ASSIGN_OR_RAISE(
      auto backpressure_node,
      insertBackpressureForDecompression(plan, assignment_by_name, current_node)
   );
   if (backpressure_node.has_value()) {
      current_node = backpressure_node.value();
   }

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
   return arrow::acero::MakeExecNode("project", &plan, {current_node}, options);
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
