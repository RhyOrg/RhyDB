#include "rhydb/common/bitmap.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <roaring/roaring.hh>

namespace rhydb {

void Bitmap::pushIfNonEmpty(
   std::vector<uint16_t>& out_keys,
   std::vector<Container>& out_containers,
   uint16_t key,
   roaring::internal::container_t* container,
   uint8_t typecode
) {
   const auto cardinality =
      static_cast<uint32_t>(roaring::internal::container_get_cardinality(container, typecode));
   if (cardinality == 0) {
      roaring::internal::container_free(container, typecode);
      return;
   }
   out_keys.push_back(key);
   out_containers.emplace_back(roaring_util::RoaringContainer{container, cardinality, typecode});
}

roaring_util::RoaringContainerView Bitmap::viewOf(const Container& container) {
   return std::visit(
      [](const auto& held) { return roaring_util::RoaringContainerView{held}; }, container
   );
}

Bitmap::Container Bitmap::copyContainer(const Container& container) {
   return std::visit(
      [](const auto& held) -> Container {
         using Held = std::decay_t<decltype(held)>;
         if constexpr (std::is_same_v<Held, roaring_util::RoaringContainerView>) {
            return held;
         } else {
            return roaring_util::RoaringContainer::clonedFrom(
               held.rawContainer(), held.getTypecode()
            );
         }
      },
      container
   );
}

Bitmap::Bitmap(const roaring::Roaring* bitmap) {
   const auto& roaring_array = bitmap->roaring.high_low_container;
   keys.reserve(roaring_array.size);
   containers.reserve(roaring_array.size);
   for (int32_t idx = 0; idx < roaring_array.size; ++idx) {
      const auto cardinality = static_cast<uint32_t>(roaring::internal::container_get_cardinality(
         roaring_array.containers[idx], roaring_array.typecodes[idx]
      ));
      keys.push_back(roaring_array.keys[idx]);
      containers.emplace_back(
         roaring_util::RoaringContainerView{
            roaring_array.containers[idx],
            cardinality,
            roaring_array.typecodes[idx],
         }
      );
   }
}

Bitmap::Bitmap(roaring::Roaring&& bitmap) {
   auto& roaring_array = bitmap.roaring.high_low_container;
   keys.reserve(roaring_array.size);
   containers.reserve(roaring_array.size);
   for (int32_t idx = 0; idx < roaring_array.size; ++idx) {
      const auto cardinality = static_cast<uint32_t>(roaring::internal::container_get_cardinality(
         roaring_array.containers[idx], roaring_array.typecodes[idx]
      ));
      keys.push_back(roaring_array.keys[idx]);
      containers.emplace_back(
         roaring_util::RoaringContainer{
            roaring_array.containers[idx],
            cardinality,
            roaring_array.typecodes[idx],
         }
      );
   }
   // The containers now belong to this object; drop the source's bookkeeping arrays without
   // freeing the containers they pointed at.
   roaring::internal::ra_clear_without_containers(&roaring_array);
}

Bitmap::Bitmap(const Bitmap& other)
    : keys(other.keys) {
   containers.reserve(other.containers.size());
   for (const auto& container : other.containers) {
      containers.push_back(copyContainer(container));
   }
}

Bitmap& Bitmap::operator=(const Bitmap& other) {
   if (this != &other) {
      std::vector<Container> containers_copy;
      containers_copy.reserve(other.containers.size());
      for (const auto& container : other.containers) {
         containers_copy.push_back(copyContainer(container));
      }
      keys = other.keys;
      containers = std::move(containers_copy);
   }
   return *this;
}

uint64_t Bitmap::cardinality() const {
   uint64_t total = 0;
   for (const auto& container : containers) {
      total += viewOf(container).getCardinality();
   }
   return total;
}

bool Bitmap::isEmpty() const {
   return keys.empty();
}

uint64_t Bitmap::andCardinality(const Bitmap& other) const {
   uint64_t total = 0;
   size_t left = 0;
   size_t right = 0;
   while (left < keys.size() && right < other.keys.size()) {
      if (keys[left] < other.keys[right]) {
         ++left;
      } else if (keys[left] > other.keys[right]) {
         ++right;
      } else {
         const auto left_view = viewOf(containers[left]);
         const auto right_view = viewOf(other.containers[right]);
         total += static_cast<uint64_t>(roaring::internal::container_and_cardinality(
            left_view.rawContainer(),
            left_view.getTypecode(),
            right_view.rawContainer(),
            right_view.getTypecode()
         ));
         ++left;
         ++right;
      }
   }
   return total;
}

Bitmap& Bitmap::operator&=(const Bitmap& other) {
   std::vector<uint16_t> result_keys;
   std::vector<Container> result_containers;
   size_t left = 0;
   size_t right = 0;
   while (left < keys.size() && right < other.keys.size()) {
      if (keys[left] < other.keys[right]) {
         ++left;
      } else if (keys[left] > other.keys[right]) {
         ++right;
      } else {
         const auto left_view = viewOf(containers[left]);
         const auto right_view = viewOf(other.containers[right]);
         uint8_t result_typecode = 0;
         auto* result_container = roaring::internal::container_and(
            left_view.rawContainer(),
            left_view.getTypecode(),
            right_view.rawContainer(),
            right_view.getTypecode(),
            &result_typecode
         );
         pushIfNonEmpty(
            result_keys, result_containers, keys[left], result_container, result_typecode
         );
         ++left;
         ++right;
      }
   }
   keys = std::move(result_keys);
   containers = std::move(result_containers);
   return *this;
}

Bitmap& Bitmap::operator-=(const Bitmap& other) {
   std::vector<uint16_t> result_keys;
   std::vector<Container> result_containers;
   size_t left = 0;
   size_t right = 0;
   while (left < keys.size()) {
      if (right >= other.keys.size() || keys[left] < other.keys[right]) {
         result_keys.push_back(keys[left]);
         result_containers.push_back(std::move(containers[left]));
         ++left;
      } else if (keys[left] > other.keys[right]) {
         ++right;
      } else {
         const auto left_view = viewOf(containers[left]);
         const auto right_view = viewOf(other.containers[right]);
         uint8_t result_typecode = 0;
         auto* result_container = roaring::internal::container_andnot(
            left_view.rawContainer(),
            left_view.getTypecode(),
            right_view.rawContainer(),
            right_view.getTypecode(),
            &result_typecode
         );
         pushIfNonEmpty(
            result_keys, result_containers, keys[left], result_container, result_typecode
         );
         ++left;
         ++right;
      }
   }
   keys = std::move(result_keys);
   containers = std::move(result_containers);
   return *this;
}

Bitmap& Bitmap::operator|=(const Bitmap& other) {
   std::vector<uint16_t> result_keys;
   std::vector<Container> result_containers;
   size_t left = 0;
   size_t right = 0;
   while (left < keys.size() || right < other.keys.size()) {
      if (right >= other.keys.size() || (left < keys.size() && keys[left] < other.keys[right])) {
         result_keys.push_back(keys[left]);
         result_containers.push_back(std::move(containers[left]));
         ++left;
      } else if (left >= keys.size() || keys[left] > other.keys[right]) {
         result_keys.push_back(other.keys[right]);
         result_containers.push_back(copyContainer(other.containers[right]));
         ++right;
      } else {
         const auto left_view = viewOf(containers[left]);
         const auto right_view = viewOf(other.containers[right]);
         uint8_t result_typecode = 0;
         auto* result_container = roaring::internal::container_or(
            left_view.rawContainer(),
            left_view.getTypecode(),
            right_view.rawContainer(),
            right_view.getTypecode(),
            &result_typecode
         );
         pushIfNonEmpty(
            result_keys, result_containers, keys[left], result_container, result_typecode
         );
         ++left;
         ++right;
      }
   }
   keys = std::move(result_keys);
   containers = std::move(result_containers);
   return *this;
}

Bitmap Bitmap::operator&(const Bitmap& other) const {
   Bitmap result;
   size_t left = 0;
   size_t right = 0;
   while (left < keys.size() && right < other.keys.size()) {
      if (keys[left] < other.keys[right]) {
         ++left;
      } else if (keys[left] > other.keys[right]) {
         ++right;
      } else {
         const auto left_view = viewOf(containers[left]);
         const auto right_view = viewOf(other.containers[right]);
         uint8_t result_typecode = 0;
         auto* result_container = roaring::internal::container_and(
            left_view.rawContainer(),
            left_view.getTypecode(),
            right_view.rawContainer(),
            right_view.getTypecode(),
            &result_typecode
         );
         pushIfNonEmpty(
            result.keys, result.containers, keys[left], result_container, result_typecode
         );
         ++left;
         ++right;
      }
   }
   return result;
}

Bitmap Bitmap::operator-(const Bitmap& other) const {
   Bitmap result = *this;
   result -= other;
   return result;
}

Bitmap Bitmap::fastUnion(const std::vector<Bitmap>& bitmaps) {
   // One cursor per input, pointing at its next container; the min-heap yields them by key.
   struct Cursor {
      uint16_t key;
      // the idx of this bitmap within the `bitmaps` vector
      size_t bitmap_idx;
      // cursor to the current position within this bitmap's containers
      size_t container_position;

      bool operator>(const Cursor& other) const { return key > other.key; }
   };
   std::priority_queue<Cursor, std::vector<Cursor>, std::greater<>> heap;
   for (size_t bitmap_idx = 0; bitmap_idx < bitmaps.size(); ++bitmap_idx) {
      if (!bitmaps[bitmap_idx].keys.empty()) {
         heap.push(
            {.key = bitmaps[bitmap_idx].keys[0], .bitmap_idx = bitmap_idx, .container_position = 0}
         );
      }
   }

   Bitmap result;
   std::vector<roaring_util::RoaringContainerView> group_views;
   const Container* last_in_group = nullptr;
   while (!heap.empty()) {
      const uint16_t key = heap.top().key;
      group_views.clear();
      while (!heap.empty() && heap.top().key == key) {
         const Cursor cursor = heap.top();
         heap.pop();
         const Bitmap& bitmap = bitmaps[cursor.bitmap_idx];
         last_in_group = &bitmap.containers[cursor.container_position];
         group_views.push_back(viewOf(*last_in_group));
         if (cursor.container_position + 1 < bitmap.keys.size()) {
            heap.push({
               .key = bitmap.keys[cursor.container_position + 1],
               .bitmap_idx = cursor.bitmap_idx,
               .container_position = cursor.container_position + 1,
            });
         }
      }

      result.keys.push_back(key);
      if (group_views.size() == 1) {
         // A container only one input has for this key is taken over as is, a view staying a view
         result.containers.push_back(copyContainer(*last_in_group));
      } else {
         result.containers.emplace_back(roaring_util::fastUnion(group_views));
      }
   }
   return result;
}

Bitmap Bitmap::fromContainerViews(
   std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>> container_views
) {
   std::erase_if(container_views, [](const auto& container_view) {
      return container_view.second.empty();
   });

   std::ranges::stable_sort(container_views, [](const auto& lhs, const auto& rhs) {
      return lhs.first < rhs.first;
   });

   Bitmap result;
   size_t idx = 0;
   while (idx < container_views.size()) {
      const uint16_t key = container_views[idx].first;
      size_t group_end = idx + 1;
      while (group_end < container_views.size() && container_views[group_end].first == key) {
         ++group_end;
      }
      if (group_end - idx == 1) {
         // A single container for this key stays a zero-copy view
         result.keys.push_back(key);
         result.containers.emplace_back(container_views[idx].second);
      } else {
         // Several containers share this key (multiple requested symbols in one 2^16 block): OR
         // them into one owning container.
         std::vector<roaring_util::RoaringContainerView> group_views;
         group_views.reserve(group_end - idx);
         for (size_t i = idx; i < group_end; ++i) {
            group_views.push_back(container_views[i].second);
         }
         result.keys.push_back(key);
         result.containers.emplace_back(roaring_util::fastUnion(group_views));
      }
      idx = group_end;
   }
   return result;
}

Bitmap Bitmap::fromChunkContainers(
   std::span<const roaring_util::RoaringContainer> chunk_containers
) {
   return fromChunks(chunk_containers.size(), [&](size_t chunk_id) {
      return roaring_util::RoaringContainerView{chunk_containers[chunk_id]};
   });
}

roaring::Roaring Bitmap::toRoaring() const {
   roaring::Roaring result;
   for (size_t idx = 0; idx < keys.size(); ++idx) {
      const auto container_view = viewOf(containers[idx]);
      auto* clone = roaring::internal::container_clone(
         container_view.rawContainer(), container_view.getTypecode()
      );
      roaring::internal::ra_append(
         &result.roaring.high_low_container, keys[idx], clone, container_view.getTypecode()
      );
   }
   return result;
}

}  // namespace rhydb
