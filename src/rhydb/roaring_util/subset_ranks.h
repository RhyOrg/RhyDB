#pragma once

#include <cstdint>
#include <vector>

#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::roaring_util {

/// The 1-based ranks, with respect to `container_a`, of the values `container_a` and `container_b`
/// have in common, in ascending order and offset by `base`.
std::vector<uint64_t> roaringSubsetRanks(
   RoaringContainerView container_a,
   RoaringContainerView container_b,
   uint32_t base
);

}  // namespace rhydb::roaring_util
