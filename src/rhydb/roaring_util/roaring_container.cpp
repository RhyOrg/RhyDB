#include "rhydb/roaring_util/roaring_container.h"

#include <type_traits>
#include <utility>
#include <variant>

#include "rhydb/common/panic.h"

namespace rhydb::roaring_util {

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

RoaringContainer RoaringContainer::clonedFrom(
   const roaring::internal::container_t* source,
   uint8_t typecode
) {
   auto* clone = roaring::internal::container_clone(source, typecode);
   RHYDB_ASSERT(clone != nullptr);
   const auto cardinality =
      static_cast<uint32_t>(roaring::internal::container_get_cardinality(clone, typecode));
   return RoaringContainer{clone, cardinality, typecode};
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
   return roaring::internal::container_size_in_bytes(container, typecode);
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

namespace {
RoaringContainer ownContainer(roaring::internal::container_t* container, uint8_t typecode) {
   const auto cardinality =
      static_cast<uint32_t>(roaring::internal::container_get_cardinality(container, typecode));
   return RoaringContainer{container, cardinality, typecode};
}
}  // namespace

RoaringContainer operator&(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty() || rhs.empty()) {
      return RoaringContainer::withCapacity(1);
   }
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_and(
      lhs.rawContainer(), lhs.getTypecode(), rhs.rawContainer(), rhs.getTypecode(), &typecode
   );
   return ownContainer(result, typecode);
}

RoaringContainer operator-(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty()) {
      return RoaringContainer::withCapacity(1);
   }
   if (rhs.empty()) {
      return lhs.toOwning();
   }
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_andnot(
      lhs.rawContainer(), lhs.getTypecode(), rhs.rawContainer(), rhs.getTypecode(), &typecode
   );
   return ownContainer(result, typecode);
}

RoaringContainer operator|(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty()) {
      return rhs.empty() ? RoaringContainer::withCapacity(1) : rhs.toOwning();
   }
   if (rhs.empty()) {
      return lhs.toOwning();
   }
   uint8_t typecode = 0;
   auto* result = roaring::internal::container_or(
      lhs.rawContainer(), lhs.getTypecode(), rhs.rawContainer(), rhs.getTypecode(), &typecode
   );
   return ownContainer(result, typecode);
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
      uint8_t result_typecode = 0;
      auto* result = roaring::internal::container_lazy_ior(
         accumulator, typecode, view.rawContainer(), view.getTypecode(), &result_typecode
      );
      if (result != accumulator) {
         roaring::internal::container_free(accumulator, typecode);
         accumulator = result;
      }
      typecode = result_typecode;
   }
   accumulator = roaring::internal::container_repair_after_lazy(accumulator, &typecode);
   return ownContainer(accumulator, typecode);
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
   uint8_t new_typecode = 0;
   auto* new_container = roaring::internal::container_ior(
      container, typecode, addend.rawContainer(), addend.getTypecode(), &new_typecode
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
            return RoaringContainer::clonedFrom(held.rawContainer(), held.getTypecode());
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

CopyOnWriteContainer& CopyOnWriteContainer::operator-=(RoaringContainerView subtrahend) {
   if (subtrahend.empty()) {
      return *this;  // removing the empty set changes nothing
   }
   RoaringContainer& owned = materializeOwned();
   owned = RoaringContainerView{owned} - subtrahend;
   return *this;
}

CopyOnWriteContainer& CopyOnWriteContainer::operator&=(RoaringContainerView other) {
   RoaringContainer& owned = materializeOwned();
   owned = RoaringContainerView{owned} & other;
   return *this;
}

}  // namespace rhydb::roaring_util
