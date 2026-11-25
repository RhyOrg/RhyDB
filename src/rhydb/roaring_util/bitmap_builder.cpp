#include "rhydb/roaring_util/bitmap_builder.h"

#include <stdexcept>
#include <utility>

namespace rhydb::roaring_util {

void BitmapBuilderByContainer::addContainer(uint16_t v_index, RoaringContainerView container) {
   if (current_v_tile_index > v_index) {
      throw std::runtime_error(
         "containers must be inserted into BitmapBuilderByContainer by increasing v_index"
      );
   }
   // first container received
   if (!current_container.has_value()) {
      current_v_tile_index = v_index;
      current_container = container.toOwning();
   }
   // current_container is finished
   else if (current_v_tile_index != v_index) {
      appendTo(result_bitmap, current_v_tile_index, std::move(*current_container));
      current_v_tile_index = v_index;
      current_container = container.toOwning();
   }
   // add a container to current_container
   else { /* current_v_tile_index == sequence_diff_key.v_index */
      *current_container |= container;
   }
}

roaring::Roaring BitmapBuilderByContainer::getBitmap() && {
   if (current_container.has_value()) {
      appendTo(result_bitmap, current_v_tile_index, std::move(*current_container));
   }
   return std::move(result_bitmap);
}

void BitmapBuilderByRange::add(uint32_t pos) {
   if (pos == current_range_end) {
      current_range_end++;
   } else {
      flush();
      current_range_start = pos;
      current_range_end = pos + 1;
   }
}

void BitmapBuilderByRange::flush() {
   if (current_range_start < current_range_end) {
      bitmap.addRange(current_range_start, current_range_end);
   }
}

roaring::Roaring BitmapBuilderByRange::getBitmap() && {
   flush();
   return std::move(bitmap);
}

}  // namespace rhydb::roaring_util
