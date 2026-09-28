#include "rhydb/query_engine/operators/transitive_closure_node.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/array/array_binary.h>
#include <arrow/builder.h>
#include <arrow/compute/exec.h>
#include <arrow/util/async_generator.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::operators {

namespace {

/// The interned directed relation: `vertex_names[i]` is the name of vertex `i`, and
/// `adjacency[i]` holds the vertices directly reachable from `i` via a single edge. `sources` are
/// the vertices to search from, in emission order - every vertex when the closure is unrestricted,
/// and the resolved `starting_from` vertices otherwise.
struct Relation {
   std::vector<std::string> vertex_names;
   std::vector<std::vector<uint32_t>> adjacency;
   std::vector<uint32_t> sources;
};

/// Validates that `column_name` is a STRING column of the input and returns its position, which
/// is also its index into the input's exec batches.
uint32_t validateAndFindColumn(
   const std::vector<schema::ColumnIdentifier>& child_schema,
   const std::string& column_name
) {
   const auto found = std::ranges::find_if(child_schema, [&](const auto& column) {
      return column.name == column_name;
   });
   CHECK_RHYDB_QUERY(
      found != child_schema.end(),
      "transitiveClosure() column '{}' is not present in the input's output schema",
      column_name
   );
   CHECK_RHYDB_QUERY(
      found->type == schema::ColumnType::STRING,
      "transitiveClosure() can only be applied to STRING columns, but column '{}' has type {}",
      column_name,
      schema::columnTypeToString(found->type)
   );
   return static_cast<uint32_t>(std::distance(child_schema.begin(), found));
}

arrow::Result<std::shared_ptr<arrow::StringArray>> asStringArray(const arrow::Datum& datum) {
   if (!datum.is_array()) {
      return arrow::Status::Invalid("transitiveClosure() expected an array-typed input column");
   }
   auto array = datum.make_array();
   if (array->type_id() != arrow::Type::STRING) {
      return arrow::Status::Invalid(
         "transitiveClosure() expected a string-typed input column, but got ",
         array->type()->ToString()
      );
   }
   return std::static_pointer_cast<arrow::StringArray>(std::move(array));
}

/// Builds the interned relation from the child's exec batches, reading the edge endpoints from
/// the given column indices. Edges with a null endpoint are skipped. Only the interned form is
/// retained: the endpoint strings are interned as the batches are scanned, so each distinct
/// vertex name is held once instead of once per edge.
///
/// `starting_from` names the vertices to search from; they are resolved here, while the interning
/// map is still around, and vertices that do not occur in the relation are dropped. Without it
/// every vertex is a source.
arrow::Result<Relation> buildRelation(
   const std::vector<std::optional<arrow::ExecBatch>>& batches,
   uint32_t from_index,
   uint32_t to_index,
   const std::optional<std::vector<std::string>>& starting_from
) {
   std::vector<std::string> vertex_names;
   std::vector<std::vector<uint32_t>> adjacency;
   std::unordered_map<std::string, uint32_t> vertex_ids;
   const auto intern = [&](std::string value) {
      const auto [iterator, inserted] =
         vertex_ids.try_emplace(std::move(value), static_cast<uint32_t>(vertex_names.size()));
      if (inserted) {
         vertex_names.push_back(iterator->first);
         adjacency.emplace_back();
      }
      return iterator->second;
   };

   for (const auto& batch : batches) {
      if (!batch.has_value()) {
         continue;
      }
      ARROW_ASSIGN_OR_RAISE(const auto from_array, asStringArray(batch->values[from_index]));
      ARROW_ASSIGN_OR_RAISE(const auto to_array, asStringArray(batch->values[to_index]));
      for (int64_t row = 0; row < from_array->length(); ++row) {
         std::optional<uint32_t> from;
         std::optional<uint32_t> to;
         if (!from_array->IsNull(row)) {
            from = intern(from_array->GetString(row));
         }
         if (!to_array->IsNull(row)) {
            to = intern(to_array->GetString(row));
         }
         if (from.has_value() && to.has_value()) {
            adjacency[*from].push_back(*to);
         }
      }
   }

   std::vector<uint32_t> sources;
   if (starting_from.has_value()) {
      sources.reserve(starting_from->size());
      for (const auto& name : starting_from.value()) {
         const auto found = vertex_ids.find(name);
         if (found != vertex_ids.end()) {
            sources.push_back(found->second);
         }
      }
   } else {
      sources.resize(vertex_names.size());
      std::iota(sources.begin(), sources.end(), 0U);
   }

   return Relation{
      .vertex_names = std::move(vertex_names),
      .adjacency = std::move(adjacency),
      .sources = std::move(sources),
   };
}

/// Emits the transitive closure of a relation in batches
class ClosureProducer {
  public:
   ClosureProducer(Relation relation, bool include_vertices, size_t batch_size)
       : relation(std::move(relation)),
         include_vertices(include_vertices),
         batch_size(batch_size),
         visited(this->relation.vertex_names.size(), 0) {}

   /// Returns the next batch of reachable pairs, or `std::nullopt` once the closure is exhausted.
   /// The order of the pairs is unspecified beyond being grouped by source vertex; callers that
   /// need an order sort the result downstream.
   arrow::Result<std::optional<arrow::ExecBatch>> nextBatch() {
      while (buffer.size() - buffer_offset < batch_size && next_source < relation.sources.size()) {
         bufferPairsOfSource(relation.sources[next_source++]);
      }
      if (buffer_offset == buffer.size()) {
         return std::nullopt;
      }
      const size_t end = std::min(buffer_offset + batch_size, buffer.size());
      ARROW_ASSIGN_OR_RAISE(auto batch, buildBatch(buffer_offset, end));
      buffer_offset = end;
      if (buffer_offset == buffer.size()) {
         buffer.clear();
         buffer_offset = 0;
      }
      return std::optional<arrow::ExecBatch>{std::move(batch)};
   }

  private:
   /// Appends every pair `(source, destination)` with `destination` reachable from `source` to the
   /// buffer, by searching the graph from `source`. Each destination is buffered as it is first
   /// discovered, so the work is proportional to the part of the graph `source` actually reaches
   /// rather than to the total number of vertices.
   void bufferPairsOfSource(uint32_t source) {
      ++visit_generation;
      frontier.clear();
      const auto discover = [&](uint32_t vertex) {
         if (visited[vertex] == visit_generation) {
            return;
         }
         visited[vertex] = visit_generation;
         frontier.push_back(vertex);
         buffer.emplace_back(source, vertex);
      };
      for (const uint32_t successor : relation.adjacency[source]) {
         discover(successor);
      }
      while (!frontier.empty()) {
         const uint32_t current = frontier.back();
         frontier.pop_back();
         for (const uint32_t successor : relation.adjacency[current]) {
            discover(successor);
         }
      }
      // Add the reflexive pair unless the vertex already reaches itself through a cycle.
      if (include_vertices && visited[source] != visit_generation) {
         buffer.emplace_back(source, source);
      }
   }

   arrow::Result<arrow::ExecBatch> buildBatch(size_t begin, size_t end) const {
      arrow::StringBuilder from_builder{};
      arrow::StringBuilder to_builder{};
      ARROW_RETURN_NOT_OK(from_builder.Reserve(static_cast<int64_t>(end - begin)));
      ARROW_RETURN_NOT_OK(to_builder.Reserve(static_cast<int64_t>(end - begin)));
      for (size_t index = begin; index < end; ++index) {
         const auto [from, to] = buffer[index];
         ARROW_RETURN_NOT_OK(from_builder.Append(relation.vertex_names[from]));
         ARROW_RETURN_NOT_OK(to_builder.Append(relation.vertex_names[to]));
      }
      arrow::Datum from_datum;
      ARROW_ASSIGN_OR_RAISE(from_datum, from_builder.Finish());
      arrow::Datum to_datum;
      ARROW_ASSIGN_OR_RAISE(to_datum, to_builder.Finish());
      return arrow::ExecBatch::Make({from_datum, to_datum});
   }

   Relation relation;
   bool include_vertices;
   size_t batch_size;
   /// Scratch state of a single-source search, kept across sources to avoid reallocating it.
   /// `visited[v]` holds the generation of the search that last reached `v`, which lets a new
   /// search start without clearing the whole vector.
   std::vector<uint64_t> visited;
   uint64_t visit_generation = 0;
   std::vector<uint32_t> frontier;
   /// The pairs found but not yet emitted, as `(source, destination)` vertex ids.
   std::vector<std::pair<uint32_t, uint32_t>> buffer;
   size_t buffer_offset = 0;
   /// Index into `relation.sources` of the next vertex to search from.
   size_t next_source = 0;
};

}  // namespace

TransitiveClosureNode::TransitiveClosureNode(
   QueryNodePtr child,
   std::string from_column,
   std::string to_column,
   bool include_vertices,
   std::optional<std::vector<std::string>> starting_from
)
    : child(std::move(child)),
      from_column(std::move(from_column)),
      to_column(std::move(to_column)),
      include_vertices(include_vertices),
      starting_from(std::move(starting_from)) {}

std::vector<schema::ColumnIdentifier> TransitiveClosureNode::getOutputSchema() const {
   return {
      {.name = std::string{FROM_COLUMN}, .type = schema::ColumnType::STRING},
      {.name = std::string{TO_COLUMN}, .type = schema::ColumnType::STRING},
   };
}

arrow::Result<arrow::acero::ExecNode*> TransitiveClosureNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options
) const {
   const auto child_schema = child->getOutputSchema();
   const uint32_t from_index = validateAndFindColumn(child_schema, from_column);
   const uint32_t to_index = validateAndFindColumn(child_schema, to_column);

   ARROW_ASSIGN_OR_RAISE(auto* child_node, child->addToExecPlan(plan, tables, query_options));

   // The child runs within this same plan. A sink drains its batches into `child_generator`;
   // the source node below collects them, computes the closure, and emits the result
   // downstream. This is the same sink -> generator -> source shape that orderBy() uses.
   arrow::AsyncGenerator<std::optional<arrow::ExecBatch>> child_generator;
   ARROW_RETURN_NOT_OK(
      arrow::acero::MakeExecNode(
         "sink", &plan, {child_node}, arrow::acero::SinkNodeOptions{&child_generator}
      )
         .status()
   );

   // `materialization_cutoff` is the batch-size-minus-one
   const size_t batch_size = query_options.materialization_cutoff + 1;
   const bool include_vertices_copy = include_vertices;
   const auto starting_from_copy = starting_from;
   // The first call builds the relation from the child's batches and hands it to the producer,
   // which then streams the closure one batch at a time as the downstream pulls.
   const auto closure = std::make_shared<std::optional<ClosureProducer>>();

   std::function<arrow::Future<std::optional<arrow::ExecBatch>>()> producer =
      [child_generator = std::move(child_generator),
       closure,
       from_index,
       to_index,
       include_vertices_copy,
       starting_from_copy,
       batch_size]() mutable -> arrow::Future<std::optional<arrow::ExecBatch>> {
      if (closure->has_value()) {
         return arrow::Future<std::optional<arrow::ExecBatch>>::MakeFinished(
            closure->value().nextBatch()
         );
      }
      return arrow::CollectAsyncGenerator(child_generator)
         .Then(
            [closure, from_index, to_index, include_vertices_copy, starting_from_copy, batch_size](
               const std::vector<std::optional<arrow::ExecBatch>>& batches
            ) -> arrow::Result<std::optional<arrow::ExecBatch>> {
               ARROW_ASSIGN_OR_RAISE(
                  Relation relation,
                  buildRelation(batches, from_index, to_index, starting_from_copy)
               );
               closure->emplace(std::move(relation), include_vertices_copy, batch_size);
               return closure->value().nextBatch();
            }
         );
   };

   const arrow::acero::SourceNodeOptions options{
      exec_node::columnsToArrowSchema(getOutputSchema()),
      std::move(producer),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", &plan, {}, options);
}

nlohmann::json TransitiveClosureNode::toJson() const {
   nlohmann::json json{
      {"type", nodeKindToString(kind())},
      {"child", child->toJson()},
      {"from", from_column},
      {"to", to_column},
      {"includeVertices", include_vertices},
   };
   if (starting_from.has_value()) {
      json["startingFrom"] = starting_from.value();
   }
   return json;
}

}  // namespace rhydb::query_engine::operators
