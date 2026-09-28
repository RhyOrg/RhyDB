#include "rhydb/common/bitmap.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
         },
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
      containers.emplace_back(roaring_util::RoaringContainerView{
         roaring_array.containers[idx], cardinality, roaring_array.typecodes[idx],
      });
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
      containers.emplace_back(roaring_util::RoaringContainer{
         roaring_array.containers[idx], cardinality, roaring_array.typecodes[idx],
      });
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
   // TODO(#1490) implement with n-way min-heap
   if (bitmaps.empty()) {
      return Bitmap{};
   }
   std::span<const Bitmap> bitmaps_span{bitmaps.data(), bitmaps.size()};
   Bitmap result = bitmaps_span.front();
   for (const auto& bitmap : bitmaps_span.subspan(1)) {
      result |= bitmap;
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
         const auto& first_view = container_views[idx].second;
         auto* accumulator =
            roaring::internal::container_clone(first_view.rawContainer(), first_view.getTypecode());
         uint8_t accumulator_typecode = first_view.getTypecode();
         for (size_t i = idx + 1; i < group_end; ++i) {
            const auto& next_view = container_views[i].second;
            uint8_t result_typecode = 0;
            auto* result_container = roaring::internal::container_ior(
               accumulator,
               accumulator_typecode,
               next_view.rawContainer(),
               next_view.getTypecode(),
               &result_typecode
            );
            if (result_container != accumulator) {
               roaring::internal::container_free(accumulator, accumulator_typecode);
               accumulator = result_container;
               accumulator_typecode = result_typecode;
            }
         }
         pushIfNonEmpty(result.keys, result.containers, key, accumulator, accumulator_typecode);
      }
      idx = group_end;
   }
   return result;
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
