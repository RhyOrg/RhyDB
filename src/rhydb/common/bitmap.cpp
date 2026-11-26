#include "rhydb/common/bitmap.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include <roaring/roaring.hh>

#include "rhydb/common/panic.h"

namespace rhydb {

void Bitmap::pushIfNonEmpty(
   std::vector<uint16_t>& out_keys,
   std::vector<Container>& out_containers,
   uint16_t key,
   roaring_util::RoaringContainer&& container
) {
   if (container.empty()) {
      return;
   }
   out_keys.push_back(key);
   out_containers.emplace_back(std::move(container));
}

Bitmap::Bitmap(const roaring::Roaring* bitmap) {
   auto views = roaring_util::containerViews(*bitmap);
   keys.reserve(views.size());
   containers.reserve(views.size());
   for (const auto& [key, view] : views) {
      keys.push_back(key);
      containers.emplace_back(view);
   }
}

Bitmap::Bitmap(roaring::Roaring&& bitmap) {
   auto owned = roaring_util::releaseContainers(std::move(bitmap));
   keys.reserve(owned.size());
   containers.reserve(owned.size());
   for (auto& [key, container] : owned) {
      keys.push_back(key);
      containers.emplace_back(std::move(container));
   }
}

Bitmap::Bitmap(const Bitmap& other) = default;

Bitmap& Bitmap::operator=(const Bitmap& other) = default;

uint64_t Bitmap::cardinality() const {
   uint64_t total = 0;
   for (const auto& container : containers) {
      total += container.getCardinality();
   }
   return total;
}

bool Bitmap::isEmpty() const {
   return keys.empty();
}

Bitmap::Container Bitmap::containerAt(uint16_t key) const {
   const auto key_it = std::ranges::lower_bound(keys, key);
   if (key_it == keys.end() || *key_it != key) {
      return Container{};
   }
   return containers[static_cast<size_t>(key_it - keys.begin())];
}

void Bitmap::append(uint16_t key, Container container) {
   RHYDB_ASSERT(keys.empty() || keys.back() < key);
   if (container.empty()) {
      return;
   }
   keys.push_back(key);
   containers.push_back(std::move(container));
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
         total +=
            roaring_util::andCardinality(containers[left].view(), other.containers[right].view());
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
         pushIfNonEmpty(
            result_keys,
            result_containers,
            keys[left],
            containers[left].view() & other.containers[right].view()
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
         pushIfNonEmpty(
            result_keys,
            result_containers,
            keys[left],
            containers[left].view() - other.containers[right].view()
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
         result_containers.push_back(other.containers[right]);
         ++right;
      } else {
         pushIfNonEmpty(
            result_keys,
            result_containers,
            keys[left],
            containers[left].view() | other.containers[right].view()
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
         pushIfNonEmpty(
            result.keys,
            result.containers,
            keys[left],
            containers[left].view() & other.containers[right].view()
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
         group_views.push_back(last_in_group->view());
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
         result.containers.push_back(*last_in_group);
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
      roaring_util::appendTo(result, keys[idx], containers[idx].toOwning());
   }
   return result;
}

}  // namespace rhydb
