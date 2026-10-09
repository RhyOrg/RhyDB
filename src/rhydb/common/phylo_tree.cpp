#include "rhydb/common/phylo_tree.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <fmt/ranges.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/vector.hpp>
#include <nlohmann/json.hpp>
#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/panic.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

namespace rhydb::common {
using rhydb::common::TreeNodeId;
using rhydb::preprocessing::PreprocessingException;

template <class Archive>
void PhyloTree::save(Archive& archive, const unsigned int /*version*/) const {
   std::vector<TreeNodeId> node_ids;
   node_ids.reserve(nodes.size());
   for (const auto& [node_id, _] : nodes) {
      node_ids.push_back(node_id);
   }
   archive & node_ids;

   for (const auto& [node_id, node] : nodes) {
      auto* called_node = node.get();
      RHYDB_ASSERT(called_node != nullptr);
      archive << *called_node;
   }
}

template <class Archive>
void PhyloTree::load(Archive& archive, const unsigned int /*version*/) {
   std::vector<TreeNodeId> node_ids;
   archive & node_ids;

   for (const auto& node_id : node_ids) {
      auto node = std::make_shared<TreeNode>();
      archive >> *node;
      nodes.emplace(node_id, std::move(node));
   }
}

template void PhyloTree::save<boost::archive::binary_oarchive>(
   boost::archive::binary_oarchive&,
   const unsigned int
) const;
template void PhyloTree::load<boost::archive::binary_iarchive>(
   boost::archive::binary_iarchive&,
   const unsigned int
);

namespace {
// NOLINTNEXTLINE(misc-no-recursion)
TreeNodeId parseAuspiceTree(
   const nlohmann::json& json,
   std::optional<TreeNodeId> parent,
   std::unordered_map<TreeNodeId, std::shared_ptr<TreeNode>>& node_map,
   int depth = 0
) {
   auto node = std::make_shared<TreeNode>();
   if (!json.contains("name")) {
      throw PreprocessingException(
         "Invalid File: Auspice JSON node does not contain a 'name' entry."
      );
   }
   node->node_id = TreeNodeId{json.at("name").get<std::string>()};
   node->parent = std::move(parent);
   node->depth = depth;
   if (json.contains("node_attrs") && json["node_attrs"].contains("div")) {
      node->branch_length = json["node_attrs"]["div"].get<float>();
   }

   const auto& children = json.contains("children") ? json["children"] : nlohmann::json::array();

   for (const auto& child : children) {
      auto child_node = parseAuspiceTree(child, node->node_id, node_map, depth + 1);
      node->children.push_back(child_node);
   }

   if (node_map.contains(node->node_id)) {
      throw PreprocessingException(
         fmt::format("Duplicate node ID found in Auspice JSON string: '{}'", node->node_id.string)
      );
   }

   node_map[node->node_id] = node;
   return node->node_id;
}

// NOLINTNEXTLINE(readability-identifier-length)
bool isValidLabelChar(char c) {
   return (isalnum(c) != 0) || c == '_' || c == '.' || c == '-' || c == '|' || c == '/' ||
          c == '\\' || c == '=' || c == '@';
}

// NOLINTNEXTLINE(readability-identifier-length)
bool isValidLength(char c) {
   return (isdigit(c) != 0) || c == '.' || c == '-' || c == '+' || c == 'e';
}

void skipIgnoredNewickTokens(std::string_view& label) {
   while (!label.empty()) {
      if (std::isspace(label.back())) {
         label.remove_suffix(1);
         continue;
      }

      if (label.back() == ']') {
         int bracket_nesting_level = 1;
         label.remove_suffix(1);
         while (!label.empty() && bracket_nesting_level > 0) {
            if (label.back() == ']') {
               ++bracket_nesting_level;
            } else if (label.back() == '[') {
               --bracket_nesting_level;
            }
            label.remove_suffix(1);
         }
         if (bracket_nesting_level != 0) {
            throw PreprocessingException("Error when parsing the Newick string - unmatched ']'");
         }
         continue;
      }

      if (label.back() == '[') {
         throw PreprocessingException("Error when parsing the Newick string - unmatched '['");
      }

      break;
   }
}

TreeNodeId parseLabel(std::string_view& label) {
   skipIgnoredNewickTokens(label);
   std::string parsed_label_string;
   while (!label.empty() && isValidLabelChar(label.back())) {
      parsed_label_string += label.back();
      label.remove_suffix(1);
   }
   skipIgnoredNewickTokens(label);
   if (label.empty() || (label.back() != ')' && label.back() != '(' && label.back() != ',')) {
      throw PreprocessingException(
         fmt::format(
            "Newick string contains invalid characters: '{}'",
            label.empty() ? std::string("<end of input>") : std::string(1, label.back())
         )
      );
   }
   std::ranges::reverse(parsed_label_string);
   return TreeNodeId{parsed_label_string};
}

TreeNodeInfo parseFullLabel(std::string_view& label) {
   std::string full_label;
   while (!label.empty() && (isValidLabelChar(label.back()) || isValidLength(label.back()))) {
      full_label += label.back();
      label.remove_suffix(1);
   }
   skipIgnoredNewickTokens(label);
   if (!label.empty() && label.back() == ':') {
      label.remove_suffix(1);
      try {
         const std::string reversed = std::string(full_label.rbegin(), full_label.rend());
         float branch_length = std::stof(reversed);
         return TreeNodeInfo{.node_id = parseLabel(label), .branch_length = branch_length};
      } catch (const std::invalid_argument& e) {
         throw PreprocessingException(
            fmt::format("Invalid branch length '{}' in Newick string", full_label)
         );
      } catch (const std::out_of_range& e) {
         throw PreprocessingException(
            fmt::format("Branch length out of range '{}' in Newick string", full_label)
         );
      }
   }
   if (!std::ranges::all_of(full_label, isValidLabelChar)) {
      throw PreprocessingException(
         fmt::format("Label of node in Newick string contains invalid characters: '{}'", full_label)
      );
   }
   skipIgnoredNewickTokens(label);
   if (label.empty() || (label.back() != ')' && label.back() != '(' && label.back() != ',')) {
      throw PreprocessingException(
         fmt::format(
            "Newick string contains invalid characters: '{}'",
            label.empty() ? std::string("<end of input>") : std::string(1, label.back())
         )
      );
   }
   std::ranges::reverse(full_label);
   return TreeNodeInfo{.node_id = TreeNodeId{full_label}};
}

// NOLINTNEXTLINE(misc-no-recursion)
TreeNodeId parseSubtree(
   std::string_view& label,
   std::unordered_map<TreeNodeId, std::shared_ptr<TreeNode>>& node_map,
   int depth = 0,
   std::optional<TreeNodeId> parent = std::nullopt
) {
   // We iterate through the string view from back to front inorder to know the name of the parent
   // node when we encounter a child node.
   auto node = std::make_shared<TreeNode>();
   node->depth = depth;
   node->parent = std::move(parent);

   skipIgnoredNewickTokens(label);
   if (label.empty()) {
      throw PreprocessingException(
         "Error when parsing the Newick string - unexpected end of input"
      );
   }
   const TreeNodeInfo tree_node_info = parseFullLabel(label);
   node->node_id = tree_node_info.node_id;
   node->branch_length = tree_node_info.branch_length;
   if (!label.empty() && label.back() == ')') {
      label.remove_suffix(1);
      depth++;
      do {
         auto child_node = parseSubtree(label, node_map, depth, node->node_id);
         node->children.push_back(child_node);
         skipIgnoredNewickTokens(label);
         if (!label.empty() && label.back() == ',') {
            label.remove_suffix(1);
         }
      } while (!label.empty() && label.back() != '(');
      if (!label.empty() && label.back() == '(') {
         label.remove_suffix(1);
         depth--;
      }
   }

   if (depth != node->depth) {
      throw PreprocessingException("Parenthesis mismatch in Newick string - depth does not match");
   }

   skipIgnoredNewickTokens(label);
   if (node_map.contains(node->node_id)) {
      throw PreprocessingException(
         fmt::format("Duplicate node ID found in Newick string: '{}'", node->node_id.string)
      );
   }
   node_map[node->node_id] = node;

   return node->node_id;
}

std::string_view trim(std::string_view label) {
   constexpr auto WHITESPACE = " \t\n\r\f\v";

   const auto start = label.find_first_not_of(WHITESPACE);
   if (start == std::string_view::npos) {
      return {};
   }
   const auto end = label.find_last_not_of(WHITESPACE);
   return label.substr(start, end - start + 1);
}

}  // namespace

PhyloTree PhyloTree::fromAuspiceJSONString(const std::string& json_string) {
   nlohmann::json json = nlohmann::json::parse(json_string);

   if (!json.contains("tree")) {
      throw PreprocessingException("Invalid File: Auspice JSON does not contain a 'tree' entry.");
   }

   PhyloTree file;
   auto root = parseAuspiceTree(json["tree"], std::nullopt, file.nodes);
   return file;
}

PhyloTree PhyloTree::fromAuspiceJSONFile(const std::filesystem::path& json_path) {
   std::ifstream file(json_path, std::ios::in | std::ios::binary);
   if (!file) {
      throw PreprocessingException(
         fmt::format("Could not open the JSON file: '{}'", json_path.string())
      );
   }

   std::ostringstream contents;
   if (file.peek() != std::ifstream::traits_type::eof()) {
      contents << file.rdbuf();
      if (contents.fail()) {
         throw PreprocessingException(
            fmt::format("Error when reading the JSON file: '{}'", json_path.string())
         );
      }
   }
   try {
      return fromAuspiceJSONString(contents.str());
   } catch (const nlohmann::json::parse_error& parse_exception) {
      throw PreprocessingException(
         fmt::format("The JSON file '{}' does not contain valid JSON.", json_path.string())
      );
   }
}

PhyloTree PhyloTree::fromNewickString(const std::string& newick_string) {
   PhyloTree file;

   std::string_view newick(newick_string);
   newick = trim(newick);
   if (newick.empty()) {
      throw PreprocessingException("Error when parsing the Newick string - The string is empty");
   }
   if (newick.back() != ';') {
      std::string shortened =
         newick_string.size() > 200 ? newick_string.substr(0, 200) + "..." : newick_string;

      throw PreprocessingException(
         fmt::format(
            "Error when parsing the Newick string: '{}' - string does not end in ';'", shortened
         )
      );
   }
   newick.remove_suffix(1);
   try {
      auto root = parseSubtree(newick, file.nodes, 0);
      if (!newick.empty()) {
         std::string shortened =
            newick_string.size() > 200 ? newick_string.substr(0, 200) + "..." : newick_string;
         throw PreprocessingException(
            fmt::format(
               "Error when parsing the Newick string: '{}' - extra characters found: '{}'",
               shortened,
               newick
            )
         );
      }
   } catch (const std::exception& e) {
      std::string shortened =
         newick_string.size() > 200 ? newick_string.substr(0, 200) + "..." : newick_string;
      throw PreprocessingException(
         fmt::format("Error when parsing the Newick string '{}': {}", shortened, e.what())
      );
   }

   return file;
}

PhyloTree PhyloTree::fromNewickFile(const std::filesystem::path& newick_path) {
   std::ifstream file(newick_path, std::ios::in | std::ios::binary);
   if (!file) {
      throw PreprocessingException(
         fmt::format("Could not open the Newick file: '{}'", newick_path.string())
      );
   }

   std::ostringstream contents;
   if (file.peek() != std::ifstream::traits_type::eof()) {
      contents << file.rdbuf();
      if (contents.fail()) {
         throw PreprocessingException(
            fmt::format("Error when reading the Newick file: '{}'", newick_path.string())
         );
      }
   }
   try {
      return fromNewickString(contents.str());
   } catch (const std::exception& e) {
      throw PreprocessingException(
         fmt::format(
            "Error when parsing the Newick string '{}': {}", newick_path.string(), e.what()
         )
      );
   }
}

PhyloTree PhyloTree::fromFile(const std::filesystem::path& path) {
   EVOBENCH_SCOPE("PhyloTree", "fromFile");

   auto ext = path.extension().string();

   std::ranges::transform(ext, ext.begin(), ::tolower);

   if (ext == ".nwk") {
      return common::PhyloTree::fromNewickFile(path);
   }
   if (ext == ".json") {
      return common::PhyloTree::fromAuspiceJSONFile(path);
   }
   throw PreprocessingException(
      fmt::format(
         "Error when parsing tree file: '{}'. Path must end with .nwk or .json", path.string()
      )
   );
}

std::optional<TreeNodeId> PhyloTree::getTreeNodeId(const std::string& node_label) const {
   auto node_id = TreeNodeId{node_label};
   if (!nodes.contains(node_id)) {
      return std::nullopt;
   }
   return node_id;
}

roaring::Roaring PhyloTree::getDescendants(const TreeNodeId& node_id) const {
   auto child_it = nodes.find(node_id);
   roaring::Roaring result_bitmap;
   if (child_it == nodes.end() || !child_it->second) {
      throw std::runtime_error(
         fmt::format("Node '{}' is null - this is an internal error.", node_id.string)
      );
   }
   std::function<void(const TreeNodeId&)> dfs = [&](const TreeNodeId& current) {
      auto current_node = nodes.find(current);
      if (!current_node->second) {
         throw std::runtime_error(
            fmt::format("Node '{}' is null - this is an internal error.", current.string)
         );
      }
      if (current_node->second->isLeaf() && current_node->second->row_index.has_value()) {
         result_bitmap.add(current_node->second->row_index.value());
      }

      for (const auto& child : current_node->second->children) {
         dfs(child);
      }
   };
   if (child_it->second->isLeaf()) {
      return result_bitmap;
   }
   dfs(node_id);
   return result_bitmap;
}

void PhyloTree::getSetOfAncestorsAtDepth(
   const std::set<TreeNodeId>& nodes_to_group,
   std::set<TreeNodeId>& ancestors_at_depth,
   int depth
) const {
   for (const auto& node_id : nodes_to_group) {
      auto node_it = nodes.find(node_id);
      while (node_it != nodes.end() && node_it->second->depth > depth) {
         if (node_it->second->parent.has_value()) {
            node_it = nodes.find(node_it->second->parent.value());
         } else {
            break;
         }
      }
      if (node_it == nodes.end()) {
         throw std::runtime_error(fmt::format("Node '{}' does not exist in tree.", node_id.string));
      }
      ancestors_at_depth.insert(node_it->first);
   }
}

MRCAResponse PhyloTree::getMRCA(const std::unordered_set<std::string>& node_labels) const {
   MRCAResponse response;
   std::set<TreeNodeId> nodes_to_group;
   int min_depth = std::numeric_limits<int>::max();

   for (const auto& node_label : node_labels) {
      auto node_it = nodes.find(TreeNodeId{node_label});
      if (node_it == nodes.end()) {
         response.not_in_tree.push_back(node_label);
      } else {
         nodes_to_group.insert(TreeNodeId{node_label});
         min_depth = std::min(node_it->second->depth, min_depth);
      }
   }
   std::ranges::sort(response.not_in_tree);

   if (nodes_to_group.empty()) {
      response.mrca_node_id = std::nullopt;
      response.parent_id_of_mrca = std::nullopt;
      return response;
   }

   std::set<TreeNodeId> set_at_min_depth;
   getSetOfAncestorsAtDepth(nodes_to_group, set_at_min_depth, min_depth);

   while (set_at_min_depth.size() > 1) {
      RHYDB_ASSERT(min_depth > 0);
      min_depth--;
      std::set<TreeNodeId> next_set_at_min_depth;
      getSetOfAncestorsAtDepth(nodes_to_group, next_set_at_min_depth, min_depth);
      set_at_min_depth = next_set_at_min_depth;
   }
   if (set_at_min_depth.empty()) {
      throw std::runtime_error(
         "No common ancestor found for the provided nodes. This is an internal error."
      );
   }

   const TreeNodeId& mrca_node_id = *set_at_min_depth.begin();
   RHYDB_ASSERT(nodes.contains(mrca_node_id));
   const std::shared_ptr<TreeNode> mrca_node = nodes.find(mrca_node_id)->second;

   response.mrca_node_id = mrca_node->node_id;
   response.parent_id_of_mrca = mrca_node->parent;
   response.mrca_depth = mrca_node->depth;
   return response;
}
namespace {
bool isInFilter(const std::string& str, const std::unordered_set<std::string>& filter) {
   return filter.contains(str);
}

std::string newickJoin(
   const std::vector<NewickFragment>& child_newick_strings,
   const std::string& self_id
) {
   std::ostringstream oss;
   oss << "(";
   bool has_value = false;
   for (size_t i = 0; i < child_newick_strings.size(); ++i) {
      // reverse the order of children to match the Newick format
      if (!child_newick_strings[child_newick_strings.size() - i - 1].fragment.has_value()) {
         continue;
      }
      if (has_value) {
         oss << ",";
      }
      oss << child_newick_strings[child_newick_strings.size() - i - 1].fragment.value();
      if (child_newick_strings[child_newick_strings.size() - i - 1].branch_length.has_value()) {
         oss << ":";
         oss << child_newick_strings[child_newick_strings.size() - i - 1].branch_length.value();
      }
      has_value = true;
   }
   if (!has_value) {
      return self_id;
   }
   oss << ")" << self_id;
   return oss.str();
}

std::optional<float> addBranchLengths(std::optional<float> left, std::optional<float> right) {
   if (left && right) {
      return *left + *right;
   }
   if (left) {
      return left;
   }
   if (right) {
      return right;
   }
   return std::nullopt;
}
}  // namespace

// NOLINTNEXTLINE(misc-no-recursion)
NewickFragment PhyloTree::partialNewickString(
   const std::unordered_set<std::string>& filter,
   const TreeNodeId& ancestor,
   bool contract_unary_nodes
) const {
   NewickFragment response;

   auto node_it = nodes.find(ancestor);
   RHYDB_ASSERT(node_it != nodes.end());
   std::vector<NewickFragment> responses;
   responses.reserve(node_it->second->children.size());
   if (node_it->second->isLeaf()) {
      if (isInFilter(ancestor.string, filter)) {
         response.fragment = std::make_optional(ancestor.string);
         response.branch_length = node_it->second->branch_length;
      } else {
         response.fragment = std::nullopt;
         response.branch_length = std::nullopt;
      }
      return response;
   }
   for (const auto& child : node_it->second->children) {
      responses.push_back(partialNewickString(filter, child, contract_unary_nodes));
   }
   std::erase_if(responses, [](const NewickFragment& resp) { return !resp.fragment.has_value(); });
   if (responses.empty()) {
      response.fragment = std::nullopt;
      response.branch_length = std::nullopt;
      return response;
   }
   if (responses.size() == 1) {
      if (contract_unary_nodes) {
         response.fragment = responses[0].fragment;
         response.branch_length =
            addBranchLengths(responses[0].branch_length, node_it->second->branch_length);
      } else {
         response.fragment = newickJoin({responses[0]}, ancestor.string);
         response.branch_length = node_it->second->branch_length;
      }
      return response;
   }
   response.fragment = newickJoin(responses, ancestor.string);
   response.branch_length = node_it->second->branch_length;
   return response;
}

NewickResponse PhyloTree::toNewickString(
   const std::unordered_set<std::string>& filter,
   bool contract_unary_nodes
) const {
   NewickResponse response;
   std::unordered_set<std::string> filter_in_tree;
   for (const auto& node_label : filter) {
      auto node_it = nodes.find(TreeNodeId{node_label});
      if (node_it == nodes.end()) {
         response.not_in_tree.push_back(node_label);
      } else {
         filter_in_tree.insert(node_label);
      }
   }
   std::ranges::sort(response.not_in_tree);
   if (filter_in_tree.empty()) {
      response.newick_string = "";
      return response;
   }
   if (filter_in_tree.size() == 1) {
      response.newick_string = *filter_in_tree.begin() + ";";
      return response;
   }

   // The MRCA will be the root of the subtree that contains all nodes in the filter.
   MRCAResponse mrca = getMRCA(filter_in_tree);
   RHYDB_ASSERT(mrca.mrca_node_id.has_value());
   auto mrca_node = nodes.find(mrca.mrca_node_id.value());
   RHYDB_ASSERT(mrca_node != nodes.end());

   NewickFragment newick_string =
      partialNewickString(filter_in_tree, mrca_node->first, contract_unary_nodes);
   RHYDB_ASSERT(newick_string.fragment.has_value());
   response.newick_string = newick_string.fragment.value() + ";";
   return response;
}

}  // namespace rhydb::common
