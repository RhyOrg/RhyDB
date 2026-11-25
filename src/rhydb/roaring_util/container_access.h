#pragma once

#include <cstdint>

#include <roaring/roaring.hh>

#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::roaring_util::detail {

/// Raw access to the roaring containers behind `RoaringContainer` and `RoaringContainerView`. Only
/// the implementation and tests of roaring_util include this header; everything else goes through
/// the wrappers' public API.
struct ContainerAccess {
   /// A roaring header for `view`, to pass to roaring's `container_*` functions while it lives.
   [[nodiscard]] static ContainerHeader header(RoaringContainerView view) { return view.header(); }

   /// The roaring container owned by `owner`.
   [[nodiscard]] static const roaring::internal::container_t* raw(const RoaringContainer& owner) {
      return owner.container;
   }

   /// The payload `view` points at, e.g. to tell whether two views share their storage.
   [[nodiscard]] static const void* payload(RoaringContainerView view) { return view.payload; }

   /// Takes ownership of a roaring-allocated, non-shared container.
   [[nodiscard]] static RoaringContainer adopt(
      roaring::internal::container_t* container,
      uint32_t cardinality,
      uint8_t typecode
   ) {
      RHYDB_ASSERT(container != nullptr);
      RHYDB_ASSERT_NE(typecode, uint8_t{SHARED_CONTAINER_TYPE});
      return RoaringContainer{container, cardinality, typecode};
   }

   /// Takes ownership of a roaring-allocated, non-shared container, computing its cardinality.
   [[nodiscard]] static RoaringContainer adopt(
      roaring::internal::container_t* container,
      uint8_t typecode
   ) {
      const auto cardinality =
         static_cast<uint32_t>(roaring::internal::container_get_cardinality(container, typecode));
      return adopt(container, cardinality, typecode);
   }

   /// Gives up ownership of the container `owner` holds, leaving `owner` empty and container-less.
   [[nodiscard]] static roaring::internal::container_t* release(RoaringContainer& owner) {
      auto* container = owner.container;
      owner.container = nullptr;
      owner.cardinality = 0;
      return container;
   }

   /// Views a non-shared roaring container owned elsewhere.
   [[nodiscard]] static RoaringContainerView view(
      const roaring::internal::container_t* container,
      uint8_t typecode,
      uint32_t cardinality
   ) {
      return RoaringContainerView::ofRawContainer(container, typecode, cardinality);
   }
};

}  // namespace rhydb::roaring_util::detail
