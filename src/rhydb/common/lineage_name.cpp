#include "rhydb/common/lineage_name.h"

#include <cstddef>

using rhydb::common::LineageName;
namespace std {
std::size_t std::hash<LineageName>::operator()(const LineageName& lineage_name) const {
   return std::hash<std::string>()(lineage_name.string);
}
}  // namespace std

namespace rhydb::common {

bool LineageName::operator==(const LineageName& other) const {
   return string == other.string;
}

}  // namespace rhydb::common