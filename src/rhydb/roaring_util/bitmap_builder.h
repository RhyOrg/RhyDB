#pragma once

#include <optional>

#include <roaring/roaring.hh>

#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::roaring_util {

/// Builds a bitmap from containers added in ascending key order; containers added for the same key
/// are OR-ed together. The added containers are copied, not borrowed.
class BitmapBuilderByContainer {
   roaring::Roaring result_bitmap;
   uint16_t current_v_tile_index = 0;
   std::optional<RoaringContainer> current_container;

  public:
   void addContainer(uint16_t v_index, RoaringContainerView container);

   roaring::Roaring getBitmap() &&;
};

class BitmapBuilderByRange {
   roaring::Roaring bitmap;

   uint32_t current_range_start = 0;
   uint32_t current_range_end = 0;

  public:
   void add(uint32_t pos);

   void flush();

   roaring::Roaring getBitmap() &&;
};

}  // namespace rhydb::roaring_util
