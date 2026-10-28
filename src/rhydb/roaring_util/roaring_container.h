#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/string.hpp>
#include <roaring/roaring.hh>

namespace rhydb::roaring_util {

class RoaringContainerView;

/// Owning RAII wrapper around a single roaring bitmap container (the 2^16-valued building block a
/// `roaring::Roaring` is internally composed of). It bundles the raw `roaring::internal` pointer,
/// its typecode and its cardinality, and encapsulates the container-level C API -- adding values,
/// sizing, run-optimizing, cloning and (de)serializing -- so callers do not reach into
/// `roaring::internal` directly.
class RoaringContainer {
   roaring::internal::container_t* container = nullptr;
   uint32_t cardinality = 0;
   uint8_t typecode = 0;

  public:
   /// Constructs an empty container that owns nothing (required for boost deserialization).
   /// Container will be in invalid state, rawContainer() must be initialized before usage
   /// Prefer `withCapacity`/`clonedFrom`/the owning constructor otherwise.
   [[deprecated(
      "RoaringContainer{} yields an invalid, container-less object and exists only for boost "
      "deserialization; use RoaringContainer::withCapacity/clonedFrom or the owning constructor"
   )]]
   RoaringContainer() = default;

   /// Takes ownership of an already-allocated container.
   RoaringContainer(
      roaring::internal::container_t* container,
      uint32_t cardinality,
      uint8_t typecode
   )
       : container(container),
         cardinality(cardinality),
         typecode(typecode) {}

   /// Creates an empty container sized for `capacity` elements
   static RoaringContainer withCapacity(int32_t capacity);

   /// Deep-copies an externally owned container and computes its cardinality.
   static RoaringContainer clonedFrom(
      const roaring::internal::container_t* source,
      uint8_t typecode
   );

   RoaringContainer(RoaringContainer&& other) noexcept
       : container(other.container),
         cardinality(other.cardinality),
         typecode(other.typecode) {
      other.container = nullptr;
   }
   RoaringContainer& operator=(RoaringContainer&& other) noexcept {
      if (this != &other) {
         std::swap(container, other.container);
         std::swap(cardinality, other.cardinality);
         std::swap(typecode, other.typecode);
      }
      return *this;
   }
   // Move-only to avoid double freeing of resources
   RoaringContainer(const RoaringContainer&) = delete;
   RoaringContainer& operator=(const RoaringContainer&) = delete;

   ~RoaringContainer() {
      if (container != nullptr) {
         roaring::internal::container_free(container, typecode);
      }
   }

   /// Adds a single 16-bit value, growing/reallocating the underlying container as needed. Like the
   /// roaring container API this blindly bumps the cardinality, so the caller must only add values
   /// that are not already present.
   void add(uint16_t value);

   /// Adds `value` if it is not yet present. Returns whether it was added.
   bool addChecked(uint16_t value);

   /// Removes `value` if it is present. Returns whether it was removed.
   bool removeChecked(uint16_t value);

   [[nodiscard]] bool contains(uint16_t value) const {
      return roaring::internal::container_contains(container, value, typecode);
   }

   RoaringContainer& operator|=(RoaringContainerView addend);
   RoaringContainer& operator-=(RoaringContainerView subtrahend);

   [[nodiscard]] uint32_t getCardinality() const { return cardinality; }

   [[nodiscard]] bool empty() const { return cardinality == 0; }

   [[nodiscard]] size_t sizeInBytes() const;

   /// Converts the container to a run container if that is more compact, then shrinks it to fit.
   void runOptimizeAndShrink();

   /// Raw handle for interop with the roaring `internal` API (e.g. BitmapBuilderByContainer,
   /// roaringSubsetRanks). The container stays owned by this object.
   [[nodiscard]] const roaring::internal::container_t* rawContainer() const { return container; }

   [[nodiscard]] uint8_t getTypecode() const { return typecode; }

   friend class boost::serialization::access;
   template <class Archive>
   void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
      if constexpr (Archive::is_saving::value) {
         // clang-format off
         archive & cardinality;
         archive & typecode;
         // clang-format on
         const size_t size_in_bytes =
            roaring::internal::container_size_in_bytes(container, typecode);
         std::string buffer(size_in_bytes, '\0');
         roaring::internal::container_write(container, typecode, buffer.data());
         archive << buffer;
      } else {
         // Free any container this object already owns before overwriting its bookkeeping, so
         // deserializing into a live container does not leak.
         if (container != nullptr) {
            roaring::internal::container_free(container, typecode);
            container = nullptr;
         }
         // clang-format off
         archive & cardinality;
         archive & typecode;
         // clang-format on
         std::string buffer;
         archive >> buffer;
         // The container-level read API takes the cardinality as a signed int32_t.
         const auto signed_cardinality = static_cast<int32_t>(cardinality);
         if (typecode == BITSET_CONTAINER_TYPE) {
            auto* bitset = roaring::internal::bitset_container_create();
            if (bitset == nullptr) {
               throw std::runtime_error("failed to allocate bitset container");
            }
            bitset_container_read(signed_cardinality, bitset, buffer.data());
            container = bitset;
         } else if (typecode == RUN_CONTAINER_TYPE) {
            auto* run = roaring::internal::run_container_create();
            if (run == nullptr) {
               throw std::runtime_error("failed to allocate run container");
            }
            run_container_read(signed_cardinality, run, buffer.data());
            container = run;
         } else if (typecode == ARRAY_CONTAINER_TYPE) {
            auto* array =
               roaring::internal::array_container_create_given_capacity(signed_cardinality);
            if (array == nullptr) {
               throw std::runtime_error("failed to allocate array container");
            }
            array_container_read(signed_cardinality, array, buffer.data());
            container = array;
         } else {
            throw std::runtime_error("unknown roaring container typecode");
         }
      }
   }
};

// On 64-bit systems we expect a 16 byte struct, on 32-bit a 12 byte struct
static_assert(sizeof(RoaringContainer) == (sizeof(void*) == 8 ? 16 : 12));

/// Non-owning view into a single roaring bitmap container.
/// The view is invalidated if the owner is destroyed or mutates the container.
class RoaringContainerView {
   const roaring::internal::container_t* container = nullptr;
   uint32_t cardinality = 0;
   uint8_t typecode = 0;

  public:
   RoaringContainerView(
      const roaring::internal::container_t* container,
      uint32_t cardinality,
      uint8_t typecode
   )
       : container(container),
         cardinality(cardinality),
         typecode(typecode) {}

   explicit RoaringContainerView(const RoaringContainer& owner)
       : container(owner.rawContainer()),
         cardinality(owner.getCardinality()),
         typecode(owner.getTypecode()) {}

   [[nodiscard]] const roaring::internal::container_t* rawContainer() const { return container; }

   [[nodiscard]] uint8_t getTypecode() const { return typecode; }

   [[nodiscard]] uint32_t getCardinality() const { return cardinality; }

   [[nodiscard]] bool empty() const { return cardinality == 0; }

   [[nodiscard]] bool contains(uint16_t value) const {
      return roaring::internal::container_contains(container, value, typecode);
   }

   [[nodiscard]] size_t sizeInBytes() const;

   [[nodiscard]] RoaringContainer toOwning() const {
      return RoaringContainer::clonedFrom(container, typecode);
   }

   /// Forward iterator over the low-16-bit values held by the container, in ascending order. The
   /// iterator borrows the container, so the view (and its owner) must outlive it and the container
   /// must not be mutated while an iteration is in progress.
   class ConstIterator {
      const roaring::internal::container_t* container = nullptr;
      uint8_t typecode = 0;
      roaring::internal::roaring_container_iterator_t internal_iterator{};
      uint16_t current_value = 0;
      // An exhausted iterator (past the last value, or a default-constructed `end()`) holds no
      // position; two exhausted iterators compare equal regardless of which container they came
      // from.
      bool exhausted = true;

     public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = uint16_t;
      using difference_type = std::ptrdiff_t;
      using pointer = const uint16_t*;
      using reference = uint16_t;

      ConstIterator() = default;

      // The container must be non-empty: `container_init_iterator` assumes a first value exists.
      ConstIterator(const roaring::internal::container_t* container, uint8_t typecode)
          : container(container),
            typecode(typecode),
            exhausted(false) {
         internal_iterator =
            roaring::internal::container_init_iterator(container, typecode, &current_value);
      }

      uint16_t operator*() const { return current_value; }

      ConstIterator& operator++() {
         exhausted = !roaring::internal::container_iterator_next(
            container, typecode, &internal_iterator, &current_value
         );
         return *this;
      }

      ConstIterator operator++(int) {
         ConstIterator copy = *this;
         ++*this;
         return copy;
      }

      bool operator==(const ConstIterator& other) const {
         if (exhausted || other.exhausted) {
            return exhausted && other.exhausted;
         }
         return container == other.container && current_value == other.current_value;
      }

      bool operator!=(const ConstIterator& other) const { return !(*this == other); }
   };

   [[nodiscard]] ConstIterator begin() const {
      // An empty container has no first value for `container_init_iterator` to load, so an empty
      // view iterates as an already-exhausted range.
      if (empty()) {
         return ConstIterator{};
      }
      return ConstIterator{container, typecode};
   }

   [[nodiscard]] static ConstIterator end() { return ConstIterator{}; }
};

[[nodiscard]] RoaringContainer operator&(RoaringContainerView lhs, RoaringContainerView rhs);
/// Cardinality of the intersection, without materializing it.
[[nodiscard]] uint32_t andCardinality(RoaringContainerView lhs, RoaringContainerView rhs);
[[nodiscard]] RoaringContainer operator-(RoaringContainerView lhs, RoaringContainerView rhs);
[[nodiscard]] RoaringContainer operator|(RoaringContainerView lhs, RoaringContainerView rhs);

/// Union of any number of containers. Like roaring's `or_many`, more than two containers are OR-ed
/// lazily -- without maintaining the cardinality after every step -- into a bitset, which is
/// repaired (and converted to its most compact representation) once at the end.
[[nodiscard]] RoaringContainer fastUnion(std::span<const RoaringContainerView> views);

/// A single roaring container that is either a non-owning view into a container owned elsewhere or
/// a privately-owned copy
/// the first mutating operation (`|=`, `-=`, `&=`) clones a borrowed container
class CopyOnWriteContainer {
   std::variant<RoaringContainerView, RoaringContainer> container;

   /// Replaces a borrowed view with a private owning copy (a no-op if already owning) and returns
   /// it for in-place mutation.
   RoaringContainer& materializeOwned();

   static RoaringContainerView emptyView();

  public:
   /// An empty container backed by a shared empty container; the first mutation materializes an
   /// owning copy.
   CopyOnWriteContainer()
       : container(emptyView()) {}

   /// Borrows an externally owned container. It must outlive this object, or at least outlive the
   /// first mutation (which clones it).
   explicit CopyOnWriteContainer(RoaringContainerView view)
       : container(view) {}

   /// Takes ownership of an already-owning container.
   explicit CopyOnWriteContainer(RoaringContainer&& owned)
       : container(std::move(owned)) {}

   CopyOnWriteContainer(CopyOnWriteContainer&&) noexcept = default;
   CopyOnWriteContainer& operator=(CopyOnWriteContainer&&) noexcept = default;
   CopyOnWriteContainer(const CopyOnWriteContainer& other);
   CopyOnWriteContainer& operator=(const CopyOnWriteContainer& other);
   ~CopyOnWriteContainer() = default;

   [[nodiscard]] RoaringContainerView view() const {
      return std::visit([](const auto& held) { return RoaringContainerView{held}; }, container);
   }

   [[nodiscard]] uint32_t getCardinality() const { return view().getCardinality(); }

   [[nodiscard]] bool empty() const { return view().empty(); }

   [[nodiscard]] RoaringContainerView::ConstIterator begin() const { return view().begin(); }
   [[nodiscard]] static RoaringContainerView::ConstIterator end() {
      return RoaringContainerView::end();
   }

   /// An independent owning copy of the current content.
   [[nodiscard]] RoaringContainer toOwning() const { return view().toOwning(); }

   /// Copy-on-write set operations: clone a borrowed container into a private copy on first write,
   /// then apply the operation. The borrowed source is never modified.
   CopyOnWriteContainer& operator|=(RoaringContainerView addend);
   CopyOnWriteContainer& operator-=(RoaringContainerView subtrahend);
   CopyOnWriteContainer& operator&=(RoaringContainerView other);
};

}  // namespace rhydb::roaring_util
