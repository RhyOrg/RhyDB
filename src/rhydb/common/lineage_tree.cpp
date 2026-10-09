#include "rhydb/common/lineage_tree.h"

#include <deque>
#include <queue>
#include <set>
#include <vector>

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <boost/algorithm/string/join.hpp>

#include "evobench/evobench.hpp"
#include "rhydb/common/panic.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

namespace rhydb::common {

namespace {

std::string edgesToString(const std::vector<Idx>& ids, const BidirectionalStringMap& lookup) {
   std::ostringstream oss;
   for (size_t i = 0; i < ids.size(); ++i) {
      oss << lookup.getValue(ids[i]);
      if (i != ids.size() - 1) {
         oss << " -> ";
      }
   }
   return oss.str();
}

class Graph {
   size_t number_of_vertices;
   std::vector<std::vector<Idx>> adjacency_list;

   std::optional<std::vector<Idx>> findCycleWitnessFromStartVertex(
      Idx start_vertex,
      std::vector<bool>& visited
   ) const;

  public:
   [[nodiscard]] std::optional<std::vector<Idx>> getCycle() const;

   explicit Graph(size_t number_of_vertices);
   void addEdge(Idx vertex_from, Idx vertex_to);
};

Graph::Graph(size_t number_of_vertices)
    : number_of_vertices(number_of_vertices),
      adjacency_list(number_of_vertices) {}

void Graph::addEdge(Idx vertex_from, Idx vertex_to) {
   RHYDB_ASSERT_LT(vertex_from, number_of_vertices);
   RHYDB_ASSERT_LT(vertex_to, number_of_vertices);
   adjacency_list.at(vertex_from).emplace_back(vertex_to);
}

std::optional<std::vector<Idx>> Graph::findCycleWitnessFromStartVertex(
   Idx start_vertex,
   std::vector<bool>& visited
) const {
   std::vector<Idx> recursion_stack;
   std::vector<bool> in_recursion_stack(number_of_vertices);

   recursion_stack.emplace_back(start_vertex);
   in_recursion_stack[start_vertex] = true;
   visited[start_vertex] = true;

   while (!recursion_stack.empty()) {
      const Idx current_vertex = recursion_stack.back();

      bool backtrack = true;

      for (const Idx neighbor : adjacency_list[current_vertex]) {
         if (in_recursion_stack[neighbor]) {
            // We found a cycle, "visit" the vertex by immediately adding it to the stack
            // as the witness and return the witness lasso
            recursion_stack.emplace_back(neighbor);
            return recursion_stack;
         }
         if (!visited[neighbor]) {
            backtrack = false;
            visited[neighbor] = true;
            recursion_stack.emplace_back(neighbor);
            in_recursion_stack[neighbor] = true;
            break;
         }
      }

      if (backtrack) {
         recursion_stack.pop_back();
         in_recursion_stack[current_vertex] = false;
      }
   }
   return std::nullopt;
}

std::optional<std::vector<Idx>> Graph::getCycle() const {
   std::vector<bool> visited(number_of_vertices, false);

   for (Idx i = 0; i < number_of_vertices; i++) {
      if (!visited[i]) {
         auto witness_lasso = findCycleWitnessFromStartVertex(i, visited);
         if (witness_lasso.has_value()) {
            // We found a witness lasso of the form 1 -> 2 -> 3 -> 4 -> 5 -> 3
            // We need to remove leading vertices up until the cycle
            RHYDB_ASSERT_GE(witness_lasso.value().size(), 2UL);
            const Idx cycle_node = witness_lasso.value().back();
            auto cycle_node_first_occurrence = std::ranges::find(witness_lasso.value(), cycle_node);
            RHYDB_ASSERT(cycle_node_first_occurrence < witness_lasso.value().end());
            witness_lasso.value().erase(witness_lasso.value().begin(), cycle_node_first_occurrence);
            return witness_lasso;
         }
      }
   }
   return std::nullopt;
}

}  // namespace

std::optional<std::vector<Idx>> containsCycle(
   size_t number_of_vertices,
   const std::vector<std::pair<Idx, Idx>>& edges
) {
   Graph graph(number_of_vertices);
   for (const auto& [from, to] : edges) {
      graph.addEdge(from, to);
   }
   return graph.getCycle();
}

std::set<Idx> LineageTree::getAllParents(
   Idx value_id,
   RecombinantEdgeFollowingMode follow_recombinant_edges
) const {
   std::set<Idx> result;
   std::vector<Idx> queue;
   queue.emplace_back(value_id);
   while (!queue.empty()) {
      auto current = queue.back();
      queue.pop_back();
      auto was_newly_inserted = result.insert(current).second;
      if (was_newly_inserted) {
         const auto& current_parents = child_to_parent_relation.at(current);
         if (current_parents.empty()) {
            continue;
         }
         if (current_parents.size() == 1) {
            queue.emplace_back(current_parents.at(0));
         } else if (follow_recombinant_edges == RecombinantEdgeFollowingMode::ALWAYS_FOLLOW) {
            for (const Idx parent : current_parents) {
               queue.emplace_back(parent);
            }
         } else if (
            follow_recombinant_edges ==
            RecombinantEdgeFollowingMode::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE
         ) {
            if (auto ancestor = recombinant_clade_ancestors.at(current)) {
               queue.emplace_back(ancestor.value());
            }
         }
      }
   }
   return result;
}

Idx LineageTree::resolveAlias(Idx value_id) const {
   if (alias_mapping.contains(value_id)) {
      return alias_mapping.at(value_id);
   }
   return value_id;
}

namespace {

std::vector<size_t> computeTopologicalRanks(
   const std::vector<std::vector<Idx>>& child_to_parent_relation,
   const std::vector<std::vector<Idx>>& child_relation
) {
   // Start with all root nodes in the queue
   std::deque<Idx> queue;
   // Keep track of the number of remaining incoming edges for all nodes in Kahn's algorithm
   std::vector<size_t> indegree(child_to_parent_relation.size());
   for (Idx node = 0; node < child_to_parent_relation.size(); ++node) {
      if (child_to_parent_relation.at(node).empty()) {
         queue.push_back(node);
      }
      indegree.at(node) = child_to_parent_relation.at(node).size();
   }

   // Kahn's algorithm for topological sort
   std::vector<size_t> topological_rank(child_to_parent_relation.size());
   size_t current_rank = 0;
   while (!queue.empty()) {
      const Idx current = queue.front();
      queue.pop_front();
      topological_rank.at(current) = current_rank++;

      for (size_t child : child_relation.at(current)) {
         indegree.at(child)--;
         if (indegree.at(child) == 0) {
            queue.push_back(child);
         }
      }
   }
   return topological_rank;
}

// We take all parents of the `recombinant_node` and walk up in the tree until they meet.
// To guarantee that all nodes walk up synchronously, we always choose the next node with the
// "maximum distance from the root". As there might be several roots and the distance would need to
// be carefully defined for multiple parents, we just take the topological rank. Because the
// topological rank of a child is guaranteed to be greater than the topological rank of all its
// parents, we will not walk past the ancestor before finding it with all nodes
std::optional<Idx> getMostRecentCommonAncestor(
   Idx recombinant_node,
   const std::vector<std::vector<Idx>>& child_to_parent_relation,
   const std::vector<size_t>& topological_rank
) {
   static auto max_heap_comparator = [](const std::pair<size_t, Idx>& left,
                                        const std::pair<size_t, Idx>& right) {
      return left.first < right.first;
   };
   RHYDB_ASSERT(child_to_parent_relation.at(recombinant_node).size() >= 2);
   std::priority_queue<
      std::pair<size_t, Idx>,
      std::vector<std::pair<size_t, Idx>>,
      decltype(max_heap_comparator)>
      priority_queue(max_heap_comparator);
   std::set<Idx> seen;
   for (auto parent : child_to_parent_relation.at(recombinant_node)) {
      seen.emplace(parent);
      priority_queue.emplace(topological_rank.at(parent), parent);
   }
   while (priority_queue.size() > 1) {
      const Idx current = priority_queue.top().second;
      priority_queue.pop();
      if (child_to_parent_relation.at(current).empty()) {
         return std::nullopt;
      }
      for (const Idx parent : child_to_parent_relation.at(current)) {
         if (!seen.contains(parent)) {
            seen.emplace(parent);
            priority_queue.emplace(topological_rank.at(parent), parent);
         }
      }
   }
   return priority_queue.top().second;
}
}  // namespace

std::unordered_map<Idx, std::optional<Idx>> LineageTree::computeRecombinantCladeAncestors(
   const std::vector<std::vector<Idx>>& child_to_parent_relation
) {
   // We also need to map from parents to children, so we also compute the inverse relation
   std::vector<std::vector<Idx>> parent_to_child_relation(child_to_parent_relation.size());
   for (Idx child = 0; child < child_to_parent_relation.size(); ++child) {
      for (const Idx parent : child_to_parent_relation.at(child)) {
         parent_to_child_relation.at(parent).emplace_back(child);
      }
   }

   // We compute the topological rank to get a measure for the distance to the root
   // The topological rank of a child is guaranteed to be greater than the topological rank of all
   // its parents
   const std::vector<size_t> topological_rank =
      computeTopologicalRanks(child_to_parent_relation, parent_to_child_relation);

   std::unordered_map<Idx, std::optional<Idx>> least_recombinant_clade_ancestors;
   for (Idx node = 0; node < child_to_parent_relation.size(); ++node) {
      if (child_to_parent_relation.at(node).size() >= 2) {
         least_recombinant_clade_ancestors.emplace(
            node, getMostRecentCommonAncestor(node, child_to_parent_relation, topological_rank)
         );
      }
   }

   return least_recombinant_clade_ancestors;
}

LineageTree LineageTree::fromEdgeList(
   size_t n_vertices,
   const std::vector<std::pair<Idx, Idx>>& edge_list,
   const BidirectionalStringMap& lookup,
   std::unordered_map<Idx, Idx>&& alias_mapping
) {
   LineageTree result;
   result.alias_mapping = alias_mapping;
   if (auto cycle = containsCycle(n_vertices, edge_list)) {
      throw preprocessing::PreprocessingException(
         fmt::format(
            "The given LineageTree contains the cycle: {}", edgesToString(cycle.value(), lookup)
         )
      );
   }
   result.child_to_parent_relation.resize(n_vertices);
   for (const auto& [parent_id, vertex_id] : edge_list) {
      result.child_to_parent_relation.at(vertex_id).emplace_back(parent_id);
   }
   result.recombinant_clade_ancestors =
      computeRecombinantCladeAncestors(result.child_to_parent_relation);
   return result;
}

LineageTreeAndIdMap::LineageTreeAndIdMap(const LineageTreeAndIdMap& other)
    : lineage_tree(other.lineage_tree),
      lineage_id_lookup_map(other.lineage_id_lookup_map.copy()),
      file(other.file) {}

LineageTreeAndIdMap& LineageTreeAndIdMap::operator=(const LineageTreeAndIdMap& other) {
   lineage_tree = other.lineage_tree;
   lineage_id_lookup_map = other.lineage_id_lookup_map.copy();
   file = other.file;
   return *this;
}

LineageTreeAndIdMap::LineageTreeAndIdMap(
   LineageTree&& lineage_tree,
   BidirectionalStringMap&& lineage_id_lookup_map,
   std::string&& file
)
    : lineage_tree(std::move(lineage_tree)),
      lineage_id_lookup_map(std::move(lineage_id_lookup_map)),
      file(std::move(file)) {}

namespace {

void assignLineageIds(
   const preprocessing::LineageDefinitionFile& file,
   BidirectionalStringMap& lookup
) {
   for (const auto& lineage : file.lineages) {
      if (lookup.getId(lineage.lineage_name.string).has_value()) {
         throw rhydb::preprocessing::PreprocessingException(
            fmt::format(
               "The lineage definitions contain the duplicate lineage '{}'", lineage.lineage_name
            )
         );
      }
      lookup.getOrCreateId(lineage.lineage_name.string);
   }
}

std::unordered_map<Idx, Idx> assignAliasIdsAndGetAliasMapping(
   const preprocessing::LineageDefinitionFile& file,
   BidirectionalStringMap& lookup
) {
   std::unordered_map<Idx, Idx> alias_mapping;
   for (const auto& lineage : file.lineages) {
      const auto lineage_id = lookup.getId(lineage.lineage_name.string);
      RHYDB_ASSERT(lineage_id.has_value());
      for (const auto& alias : lineage.aliases) {
         if (lookup.getId(alias.string).has_value()) {
            throw rhydb::preprocessing::PreprocessingException(
               fmt::format(
                  "The alias '{}' for lineage '{}' is already defined as a lineage or another "
                  "alias.",
                  alias,
                  lineage.lineage_name
               )
            );
         }
         auto alias_id = lookup.getOrCreateId(alias.string);
         alias_mapping[alias_id] = lineage_id.value();
      }
   }
   return alias_mapping;
}

std::vector<std::pair<Idx, Idx>> getParentChildEdges(
   const preprocessing::LineageDefinitionFile& file,
   const BidirectionalStringMap& lookup,
   const std::unordered_map<Idx, Idx>& alias_mapping
) {
   std::vector<std::pair<Idx, Idx>> edge_list;
   for (const auto& lineage : file.lineages) {
      const auto child_id = lookup.getId(lineage.lineage_name.string);
      RHYDB_ASSERT(child_id.has_value());

      for (const auto& parent_lineage : lineage.parents) {
         auto parent_id = lookup.getId(parent_lineage.string);
         if (!parent_id.has_value()) {
            throw preprocessing::PreprocessingException(
               fmt::format(
                  "The lineage '{}' which is specified as the parent of vertex '{}' does not have "
                  "a "
                  "definition itself.",
                  parent_lineage,
                  lineage.lineage_name
               )
            );
         }
         if (alias_mapping.contains(parent_id.value())) {
            parent_id = alias_mapping.at(parent_id.value());
         }
         edge_list.emplace_back(parent_id.value(), child_id.value());
      }
   }
   return edge_list;
}

}  // namespace

LineageTreeAndIdMap LineageTreeAndIdMap::fromLineageDefinitionFile(
   preprocessing::LineageDefinitionFile&& file
) {
   BidirectionalStringMap lookup;
   assignLineageIds(file, lookup);
   std::unordered_map<Idx, Idx> alias_mapping = assignAliasIdsAndGetAliasMapping(file, lookup);

   const std::vector<std::pair<Idx, Idx>> edge_list =
      getParentChildEdges(file, lookup, alias_mapping);
   auto lineage_tree =
      LineageTree::fromEdgeList(file.lineages.size(), edge_list, lookup, std::move(alias_mapping));
   return {std::move(lineage_tree), std::move(lookup), std::move(file.raw_file)};
}

LineageTreeAndIdMap LineageTreeAndIdMap::fromLineageDefinitionFilePath(
   const std::filesystem::path& file_path
) {
   EVOBENCH_SCOPE("LineageTreeAndIdMap", "fromLineageDefinitionFilePath");
   auto definition_file = preprocessing::LineageDefinitionFile::fromYAMLFile(file_path);

   return fromLineageDefinitionFile(std::move(definition_file));
}

}  // namespace rhydb::common
