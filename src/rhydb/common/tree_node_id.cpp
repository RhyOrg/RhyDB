#include "rhydb/common/tree_node_id.h"

#include <cstddef>

std::size_t std::hash<rhydb::common::TreeNodeId>::operator()(
   const rhydb::common::TreeNodeId& tree_node_id
) const {
   return std::hash<std::string>()(tree_node_id.string);
}

namespace rhydb::common {

bool TreeNodeId::operator==(const TreeNodeId& other) const {
   return string == other.string;
}

bool TreeNodeId::operator<(const TreeNodeId& other) const {
   return string < other.string;
}

}  // namespace rhydb::common