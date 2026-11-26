#include "rhydb/roaring_util/roaring_container.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

#include "rhydb/common/panic.h"
#include "rhydb/roaring_util/container_access.h"

namespace rhydb::roaring_util {

using detail::ContainerAccess;

RoaringContainer RoaringContainer::withCapacity(int32_t capacity) {
   roaring::internal::container_t* container;
   uint8_t typecode;
   // roaring::internal::DEFAULT_MAX_SIZE is the maximum size for array containers.
   if (capacity <= roaring::internal::DEFAULT_MAX_SIZE) {
      container = roaring::internal::array_container_create_given_capacity(capacity);
      typecode = ARRAY_CONTAINER_TYPE;
   } else {
      container = roaring::internal::bitset_container_create();
      typecode = BITSET_CONTAINER_TYPE;
   }
   RHYDB_ASSERT(container != nullptr);
   return RoaringContainer{container, 0, typecode};
}

RoaringContainer RoaringContainer::ofRange(uint32_t start, uint32_t end) {
   RHYDB_ASSERT_LE(start, end);
   RHYDB_ASSERT_LE(end, uint32_t{1} << 16);
   if (start == end) {
      return withCapacity(1);
   }
   uint8_t typecode = 0;
   auto* container = roaring::internal::container_range_of_ones(start, end, &typecode);
   RHYDB_ASSERT(container != nullptr);
   return RoaringContainer{container, end - start, typecode};
}

void RoaringContainer::add(uint16_t value) {
   uint8_t new_typecode;
   auto* new_container =
      roaring::internal::container_add(container, value, typecode, &new_typecode);
   if (new_container != container) {
      roaring::internal::container_free(container, typecode);
      container = new_container;
      typecode = new_typecode;
   }
   cardinality += 1;
}

bool RoaringContainer::addChecked(uint16_t value) {
   if (contains(value)) {
      return false;
   }
   add(value);
   return true;
}

bool RoaringContainer::removeChecked(uint16_t value) {
   if (!contains(value)) {
      return false;
   }
   uint8_t new_typecode;
   auto* new_container =
      roaring::internal::container_remove(container, value, typecode, &new_typecode);
   if (new_container != container) {
      roaring::internal::container_free(container, typecode);
      container = new_container;
   }
   typecode = new_typecode;
   cardinality -= 1;
   return true;
}

size_t RoaringContainer::sizeInBytes() const {
   return roaring::internal::container_size_in_bytes(container, typecode);
}

size_t RoaringContainerView::sizeInBytes() const {
   const detail::ContainerHeader container = header();
   return roaring::internal::container_size_in_bytes(container.get(), typecode);
}

RoaringContainerView RoaringContainerView::fromPayload(
   ContainerType type,
   uint32_t cardinality,
   std::span<const std::byte> payload
) {
   const uint8_t typecode = detail::toTypecode(type);
   size_t expected_size = 0;
   size_t alignment = alignof(uint16_t);
   size_t n_runs = 0;
   detail::ContainerPayload typed_payload{.array = nullptr};
   // The alignment is asserted below, before the view reads through these pointers.
   // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
   switch (typecode) {
      case BITSET_CONTAINER_TYPE:
         expected_size = BITSET_CONTAINER_WORDS * sizeof(uint64_t);
         alignment = alignof(uint64_t);
         typed_payload.bitset_words = reinterpret_cast<const uint64_t*>(payload.data());
         break;
      case ARRAY_CONTAINER_TYPE:
         expected_size = size_t{cardinality} * sizeof(uint16_t);
         typed_payload.array = reinterpret_cast<const uint16_t*>(payload.data());
         break;
      case RUN_CONTAINER_TYPE:
         n_runs = payload.size() / sizeof(roaring::internal::rle16_t);
         expected_size = n_runs * sizeof(roaring::internal::rle16_t);
         alignment = alignof(roaring::internal::rle16_t);
         typed_payload.runs = reinterpret_cast<const roaring::internal::rle16_t*>(payload.data());
         break;
      default:
         RHYDB_PANIC("unknown roaring container typecode {}", typecode);
   }
   // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
   RHYDB_ASSERT_EQ(payload.size(), expected_size);
   RHYDB_ASSERT_EQ(reinterpret_cast<std::uintptr_t>(payload.data()) % alignment, uintptr_t{0});
   RHYDB_ASSERT_LE(cardinality, uint32_t{1} << 16);
   // Runs are disjoint and not adjacent, so a container holds at most 2^15 of them.
   RHYDB_ASSERT_LE(n_runs, size_t{1} << 15);

   const RoaringContainerView view{
      typed_payload,
      cardinality,
      static_cast<uint16_t>(n_runs),
      typecode,
   };
   RHYDB_DEBUG_ASSERT_EQ(
      static_cast<uint32_t>(
         roaring::internal::container_get_cardinality(view.header().get(), typecode)
      ),
      cardinality
   );
   return view;
}

std::span<const std::byte> RoaringContainerView::payload() const {
   size_t size_in_bytes = 0;
   switch (typecode) {
      case BITSET_CONTAINER_TYPE:
         size_in_bytes = BITSET_CONTAINER_WORDS * sizeof(uint64_t);
         break;
      case ARRAY_CONTAINER_TYPE:
         size_in_bytes = size_t{cardinality} * sizeof(uint16_t);
         break;
      case RUN_CONTAINER_TYPE:
         size_in_bytes = size_t{n_runs} * sizeof(roaring::internal::rle16_t);
         break;
      default:
         RHYDB_PANIC("unknown roaring container typecode {}", typecode);
   }
   return {static_cast<const std::byte*>(payloadAddress()), size_in_bytes};
}

RoaringContainer RoaringContainerView::toOwning() const {
   if (empty()) {
      return RoaringContainer::withCapacity(1);
   }
   const detail::ContainerHeader container = header();
   auto* clone = roaring::internal::container_clone(container.get(), typecode);
   RHYDB_ASSERT(clone != nullptr);
   return RoaringContainer{clone, cardinality, typecode};
}

void RoaringContainer::runOptimizeAndShrink() {
   uint8_t new_typecode;
   auto* new_container =
      roaring::internal::convert_run_optimize(container, typecode, &new_typecode);
   if (new_container != container) {
      container = new_container;
      typecode = new_typecode;
   }
   roaring::internal::container_shrink_to_fit(container, typecode);
}

RoaringContainer operator&(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty() || rhs.empty()) {
      return RoaringContainer::withCapacity(1);
   }
   const auto lhs_container = ContainerAccess::header(lhs);
   const auto rhs_container = ContainerAccess::header(rhs);
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_and(
      lhs_container.get(),
      lhs_container.getTypecode(),
      rhs_container.get(),
      rhs_container.getTypecode(),
      &typecode
   );
   return ContainerAccess::adopt(result, typecode);
}

RoaringContainer operator-(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty()) {
      return RoaringContainer::withCapacity(1);
   }
   if (rhs.empty()) {
      return lhs.toOwning();
   }
   const auto lhs_container = ContainerAccess::header(lhs);
   const auto rhs_container = ContainerAccess::header(rhs);
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_andnot(
      lhs_container.get(),
      lhs_container.getTypecode(),
      rhs_container.get(),
      rhs_container.getTypecode(),
      &typecode
   );
   return ContainerAccess::adopt(result, typecode);
}

RoaringContainer operator|(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty()) {
      return rhs.empty() ? RoaringContainer::withCapacity(1) : rhs.toOwning();
   }
   if (rhs.empty()) {
      return lhs.toOwning();
   }
   const auto lhs_container = ContainerAccess::header(lhs);
   const auto rhs_container = ContainerAccess::header(rhs);
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_or(
      lhs_container.get(),
      lhs_container.getTypecode(),
      rhs_container.get(),
      rhs_container.getTypecode(),
      &typecode
   );
   return ContainerAccess::adopt(result, typecode);
}

RoaringContainer complement(RoaringContainerView view, uint32_t universe_size) {
   RHYDB_ASSERT_LE(universe_size, uint32_t{1} << 16);
   if (view.empty()) {
      return RoaringContainer::ofRange(0, universe_size);
   }
   const auto container = ContainerAccess::header(view);
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_not_range(
      container.get(), container.getTypecode(), 0, universe_size, &typecode
   );
   return ContainerAccess::adopt(result, typecode);
}

RoaringContainer fastUnion(std::span<const RoaringContainerView> views) {
   if (views.empty()) {
      return RoaringContainer::withCapacity(1);
   }
   if (views.size() == 1) {
      return views[0].toOwning();
   }
   if (views.size() == 2) {
      return views[0] | views[1];
   }
   // Accumulating in a bitset makes each OR linear in the added container, instead of re-merging an
   // ever-growing sorted array container
   roaring::internal::container_t* accumulator = roaring::internal::bitset_container_create();
   RHYDB_ASSERT(accumulator != nullptr);
   uint8_t typecode = BITSET_CONTAINER_TYPE;
   for (const auto& view : views) {
      if (view.empty()) {
         continue;
      }
      const auto container = ContainerAccess::header(view);
      uint8_t result_typecode = 0;
      auto* result = roaring::internal::container_lazy_ior(
         accumulator, typecode, container.get(), container.getTypecode(), &result_typecode
      );
      if (result != accumulator) {
         roaring::internal::container_free(accumulator, typecode);
         accumulator = result;
      }
      typecode = result_typecode;
   }
   accumulator = roaring::internal::container_repair_after_lazy(accumulator, &typecode);
   return ContainerAccess::adopt(accumulator, typecode);
}

std::vector<std::pair<uint16_t, RoaringContainerView>> containerViews(
   const roaring::Roaring& bitmap
) {
   const auto& roaring_array = bitmap.roaring.high_low_container;
   std::vector<std::pair<uint16_t, RoaringContainerView>> views;
   views.reserve(static_cast<size_t>(roaring_array.size));
   for (int32_t idx = 0; idx < roaring_array.size; ++idx) {
      uint8_t typecode = roaring_array.typecodes[idx];
      const auto* container =
         roaring::internal::container_unwrap_shared(roaring_array.containers[idx], &typecode);
      const auto cardinality =
         static_cast<uint32_t>(roaring::internal::container_get_cardinality(container, typecode));
      views.emplace_back(
         roaring_array.keys[idx], ContainerAccess::view(container, typecode, cardinality)
      );
   }
   return views;
}

std::vector<std::pair<uint16_t, RoaringContainer>> releaseContainers(roaring::Roaring&& bitmap) {
   auto& roaring_array = bitmap.roaring.high_low_container;
   std::vector<std::pair<uint16_t, RoaringContainer>> containers;
   containers.reserve(static_cast<size_t>(roaring_array.size));
   for (int32_t idx = 0; idx < roaring_array.size; ++idx) {
      uint8_t typecode = roaring_array.typecodes[idx];
      // A shared container hands back its wrapped container if this was the last reference to it,
      // and a clone otherwise.
      auto* container =
         roaring::internal::get_writable_copy_if_shared(roaring_array.containers[idx], &typecode);
      containers.emplace_back(roaring_array.keys[idx], ContainerAccess::adopt(container, typecode));
   }
   // The containers now belong to the result; drop the bookkeeping arrays without freeing the
   // containers they pointed at.
   roaring::internal::ra_clear_without_containers(&roaring_array);
   return containers;
}

void appendTo(roaring::Roaring& bitmap, uint16_t key, RoaringContainer&& container) {
   if (container.empty()) {
      return;
   }
   auto& roaring_array = bitmap.roaring.high_low_container;
   RHYDB_ASSERT(roaring_array.size == 0 || roaring_array.keys[roaring_array.size - 1] < key);
   const uint8_t typecode = ContainerAccess::typecode(container);
   roaring::internal::ra_append(&roaring_array, key, ContainerAccess::release(container), typecode);
}

RoaringContainer& RoaringContainer::operator|=(RoaringContainerView addend) {
   // `container_ior` requires both operands to be non-empty: the mixed array/run unions read
   // element 0 of the array before checking its cardinality, so an empty array would inject
   // whatever stale value its buffer holds into the result.
   if (addend.empty()) {
      return *this;
   }
   if (empty()) {
      *this = addend.toOwning();
      return *this;
   }
   // OR-ing a container into itself changes nothing. It must not reach `container_ior`, which
   // could reallocate the payload the addend's header still points at.
   if (addend.payloadAddress() == RoaringContainerView{*this}.payloadAddress()) {
      return *this;
   }
   const detail::ContainerHeader addend_container = addend.header();
   uint8_t new_typecode = 0;
   auto* new_container = roaring::internal::container_ior(
      container, typecode, addend_container.get(), addend.typecode, &new_typecode
   );
   if (new_container != container) {
      // The union could not be computed in place, so a new container was allocated and freeing the
      // old one is up to us.
      roaring::internal::container_free(container, typecode);
      container = new_container;
   }
   typecode = new_typecode;
   cardinality =
      static_cast<uint32_t>(roaring::internal::container_get_cardinality(container, typecode));
   return *this;
}

RoaringContainer& RoaringContainer::operator-=(RoaringContainerView subtrahend) {
   if (subtrahend.empty() || empty()) {
      return *this;
   }
   *this = RoaringContainerView{*this} - subtrahend;
   return *this;
}

namespace {
// Deep-copies the copy-on-write state: a borrowing state re-borrows the same container (cheap), an
// owning state is cloned into an independent container.
std::variant<RoaringContainerView, RoaringContainer> cloneCowState(
   const std::variant<RoaringContainerView, RoaringContainer>& source
) {
   return std::visit(
      [](const auto& held) -> std::variant<RoaringContainerView, RoaringContainer> {
         using Held = std::decay_t<decltype(held)>;
         if constexpr (std::is_same_v<Held, RoaringContainerView>) {
            return held;
         } else {
            return RoaringContainerView{held}.toOwning();
         }
      },
      source
   );
}
}  // namespace

CopyOnWriteContainer::CopyOnWriteContainer(const CopyOnWriteContainer& other)
    : container(cloneCowState(other.container)) {}

CopyOnWriteContainer& CopyOnWriteContainer::operator=(const CopyOnWriteContainer& other) {
   if (this != &other) {
      container = cloneCowState(other.container);
   }
   return *this;
}

RoaringContainerView CopyOnWriteContainer::emptyView() {
   static const RoaringContainer empty = RoaringContainer::withCapacity(1);
   return RoaringContainerView{empty};
}

RoaringContainer& CopyOnWriteContainer::materializeOwned() {
   if (std::holds_alternative<RoaringContainerView>(container)) {
      container = std::get<RoaringContainerView>(container).toOwning();
   }
   return std::get<RoaringContainer>(container);
}

CopyOnWriteContainer& CopyOnWriteContainer::operator|=(RoaringContainerView addend) {
   if (addend.empty()) {
      return *this;  // union with the empty set changes nothing
   }
   materializeOwned() |= addend;
   return *this;
}

// The difference and intersection are computed into a new container anyway, so a borrowed
// container is not cloned first.
CopyOnWriteContainer& CopyOnWriteContainer::operator-=(RoaringContainerView subtrahend) {
   if (subtrahend.empty()) {
      return *this;  // removing the empty set changes nothing
   }
   container = view() - subtrahend;
   return *this;
}

CopyOnWriteContainer& CopyOnWriteContainer::operator&=(RoaringContainerView other) {
   container = view() & other;
   return *this;
}

}  // namespace rhydb::roaring_util
