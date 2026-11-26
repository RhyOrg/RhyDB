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

#include "rhydb/common/panic.h"

namespace rhydb::roaring_util {

class RoaringContainer;
class RoaringContainerView;

/// Number of 64-bit words of a bitset container, which holds one bit per value of its 2^16 range.
inline constexpr size_t BITSET_CONTAINER_WORDS = roaring::internal::BITSET_CONTAINER_SIZE_IN_WORDS;

/// The representation a roaring container stores its values in: a sorted array, a bitset of the
/// whole 2^16 range, or a list of runs.
enum class ContainerType : uint8_t {
   ARRAY = 2,
   BITSET = 1,
   RUN = 3,
};

namespace detail {

// The values equal roaring's typecodes, so converting between the two is a cast.
static_assert(static_cast<uint8_t>(ContainerType::ARRAY) == ARRAY_CONTAINER_TYPE);
static_assert(static_cast<uint8_t>(ContainerType::BITSET) == BITSET_CONTAINER_TYPE);
static_assert(static_cast<uint8_t>(ContainerType::RUN) == RUN_CONTAINER_TYPE);

/// Roaring's typecode of a container type.
[[nodiscard]] constexpr uint8_t toTypecode(ContainerType type) {
   return static_cast<uint8_t>(type);
}

/// The container type of one of roaring's non-shared typecodes.
[[nodiscard]] constexpr ContainerType toContainerType(uint8_t typecode) {
   return static_cast<ContainerType>(typecode);
}

/// Raw access to the roaring containers behind `RoaringContainer` and `RoaringContainerView`, for
/// the implementation and tests of roaring_util only. Defined in `container_access.h`.
struct ContainerAccess;

/// The borrowed payload of a container: a pointer to the sorted array values, the bitset words or
/// the runs. Which member is active is determined by the container's typecode; only that member
/// may be read.
union ContainerPayload {
   const uint16_t* array;
   const uint64_t* bitset_words;
   const roaring::internal::rle16_t* runs;
};

/// Roaring's per-type container header (`array_container_t`, `bitset_container_t` or
/// `run_container_t`), built from a view's fields for the duration of a call into roaring's generic
/// `container_*` functions, which dispatch on the typecode themselves. The header only borrows the
/// payload, and is only ever passed as `const container_t*`, so roaring never writes through it.
class ContainerHeader {
   union {
      roaring::internal::array_container_t array;
      roaring::internal::bitset_container_t bitset;
      roaring::internal::run_container_t run;
   } header{};
   uint8_t typecode = ARRAY_CONTAINER_TYPE;

  public:
   ContainerHeader() = default;

   ContainerHeader(
      ContainerPayload payload,
      uint32_t cardinality,
      uint16_t n_runs,
      uint8_t typecode
   )
       : typecode(typecode) {
      // Roaring's headers hold mutable payload pointers, but this header is only ever handed out
      // as `const container_t*`.
      // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
      switch (typecode) {
         case BITSET_CONTAINER_TYPE:
            header.bitset.cardinality = static_cast<int32_t>(cardinality);
            header.bitset.words = const_cast<uint64_t*>(payload.bitset_words);
            break;
         case RUN_CONTAINER_TYPE:
            header.run.n_runs = n_runs;
            header.run.capacity = n_runs;
            header.run.runs = const_cast<roaring::internal::rle16_t*>(payload.runs);
            break;
         default:
            RHYDB_DEBUG_ASSERT_EQ(typecode, uint8_t{ARRAY_CONTAINER_TYPE});
            header.array.cardinality = static_cast<int32_t>(cardinality);
            header.array.capacity = static_cast<int32_t>(cardinality);
            header.array.array = const_cast<uint16_t*>(payload.array);
            break;
      }
      // NOLINTEND(cppcoreguidelines-pro-type-const-cast)
   }

   /// The header as roaring's opaque container type. Points into this object, so it must not
   /// outlive it.
   [[nodiscard]] const roaring::internal::container_t* get() const {
      switch (typecode) {
         case BITSET_CONTAINER_TYPE:
            return &header.bitset;
         case RUN_CONTAINER_TYPE:
            return &header.run;
         default:
            return &header.array;
      }
   }

   [[nodiscard]] uint8_t getTypecode() const { return typecode; }

   /// The borrowed payload: the array values, the bitset words or the runs.
   [[nodiscard]] const void* payload() const {
      switch (typecode) {
         case BITSET_CONTAINER_TYPE:
            return header.bitset.words;
         case RUN_CONTAINER_TYPE:
            return header.run.runs;
         default:
            return header.array.array;
      }
   }
};

}  // namespace detail

/// Owning RAII wrapper around a single roaring bitmap container (the 2^16-valued building block a
/// `roaring::Roaring` is internally composed of). It bundles the roaring-allocated container, its
/// typecode and its cardinality, and encapsulates the container-level C API -- adding values,
/// sizing, run-optimizing, cloning and (de)serializing -- so callers do not reach into
/// `roaring::internal` directly. The container is never a roaring shared container.
class RoaringContainer {
   roaring::internal::container_t* container = nullptr;
   uint32_t cardinality = 0;
   uint8_t typecode = 0;

   /// Takes ownership of an already-allocated, non-shared container.
   RoaringContainer(
      roaring::internal::container_t* container,
      uint32_t cardinality,
      uint8_t typecode
   )
       : container(container),
         cardinality(cardinality),
         typecode(typecode) {}

   friend class RoaringContainerView;
   friend struct detail::ContainerAccess;

  public:
   /// Constructs an empty array container.
   RoaringContainer()
       : RoaringContainer(withCapacity(0)) {}

   /// Creates an empty container sized for `capacity` elements
   static RoaringContainer withCapacity(int32_t capacity);

   /// Creates a container holding exactly the values in `[start, end)`, with `end <= 2^16`
   static RoaringContainer ofRange(uint32_t start, uint32_t end);

   /// Creates a bitset container whose 64-bit words (all zero initially) are written by
   /// `fill(std::span<uint64_t, BITSET_CONTAINER_WORDS>)`; bit `i % 64` of word `i / 64`
   /// holds value `i`. The cardinality is computed afterwards.
   template <typename Fill>
   static RoaringContainer bitsetFilledBy(Fill&& fill) {
      auto* bitset = roaring::internal::bitset_container_create();
      RHYDB_ASSERT(bitset != nullptr);
      // Owned before `fill` runs, so the bitset is freed should `fill` throw.
      RoaringContainer result{bitset, 0, BITSET_CONTAINER_TYPE};
      std::forward<Fill>(fill)(
         std::span<uint64_t, BITSET_CONTAINER_WORDS>{bitset->words, BITSET_CONTAINER_WORDS}
      );
      bitset->cardinality = roaring::internal::bitset_container_compute_cardinality(bitset);
      result.cardinality = static_cast<uint32_t>(bitset->cardinality);
      return result;
   }

   // After move construction the source holds no container; after move assignment it holds the
   // destination's previous container. Either way, it may only be destroyed or assigned to.
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

   [[nodiscard]] ContainerType getType() const { return detail::toContainerType(typecode); }

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

/// Non-owning view of a single roaring bitmap container: its typecode, its cardinality and a
/// pointer to its payload (the sorted array values, the bitset words or the runs), which is all
/// that is needed to rebuild roaring's container header on demand. The view does not point at a
/// roaring container header, so it can also view a payload stored elsewhere, e.g. in a flat buffer
/// (`fromPayload`). It is invalidated if the owner is destroyed or mutates the container.
class RoaringContainerView {
   detail::ContainerPayload payload_data{.array = nullptr};
   uint32_t cardinality = 0;
   /// Number of runs of a run container, 0 otherwise. A run container holds at most 2^15 runs.
   uint16_t n_runs = 0;
   uint8_t typecode = ARRAY_CONTAINER_TYPE;

   RoaringContainerView(
      detail::ContainerPayload payload_data,
      uint32_t cardinality,
      uint16_t n_runs,
      uint8_t typecode
   )
       : payload_data(payload_data),
         cardinality(cardinality),
         n_runs(n_runs),
         typecode(typecode) {}

   /// Views a non-shared roaring container by reading its header once.
   static RoaringContainerView ofRawContainer(
      const roaring::internal::container_t* container,
      uint8_t typecode,
      uint32_t cardinality
   ) {
      switch (typecode) {
         case BITSET_CONTAINER_TYPE:
            return {
               {.bitset_words =
                   static_cast<const roaring::internal::bitset_container_t*>(container)->words},
               cardinality,
               0,
               typecode,
            };
         case RUN_CONTAINER_TYPE: {
            const auto* run = static_cast<const roaring::internal::run_container_t*>(container);
            return {
               {.runs = run->runs},
               cardinality,
               static_cast<uint16_t>(run->n_runs),
               typecode,
            };
         }
         default:
            RHYDB_DEBUG_ASSERT_EQ(typecode, uint8_t{ARRAY_CONTAINER_TYPE});
            return {
               {.array =
                   static_cast<const roaring::internal::array_container_t*>(container)->array},
               cardinality,
               0,
               typecode,
            };
      }
   }

   [[nodiscard]] detail::ContainerHeader header() const {
      return detail::ContainerHeader{payload_data, cardinality, n_runs, typecode};
   }

   /// The address of the payload (read through the active member), to tell whether two views
   /// borrow the same storage.
   [[nodiscard]] const void* payloadAddress() const {
      switch (typecode) {
         case BITSET_CONTAINER_TYPE:
            return payload_data.bitset_words;
         case RUN_CONTAINER_TYPE:
            return payload_data.runs;
         default:
            return payload_data.array;
      }
   }

   friend class RoaringContainer;
   friend struct detail::ContainerAccess;

  public:
   /// An empty view.
   RoaringContainerView() = default;

   /// Views a container payload stored outside of a roaring container, e.g. in a flat,
   /// pointer-free buffer, in the representation `payload()` returns:
   /// - array: the `cardinality` sorted values as `uint16_t`, aligned to 2 bytes;
   /// - bitset: the `BITSET_CONTAINER_WORDS` words as `uint64_t`, aligned to 8 bytes;
   /// - run: the `{value, length - 1}` runs as pairs of `uint16_t`, aligned to 2 bytes.
   /// Values are in native byte order. `cardinality` is the number of values the payload holds.
   /// The payload must outlive the view and must not be mutated while it exists. Panics if the
   /// payload size or alignment does not match the type.
   [[nodiscard]] static RoaringContainerView fromPayload(
      ContainerType type,
      uint32_t cardinality,
      std::span<const std::byte> payload
   );

   explicit RoaringContainerView(const RoaringContainer& owner) {
      if (owner.container != nullptr) {
         *this = ofRawContainer(owner.container, owner.typecode, owner.cardinality);
      }
   }

   [[nodiscard]] ContainerType getType() const { return detail::toContainerType(typecode); }

   [[nodiscard]] uint32_t getCardinality() const { return cardinality; }

   [[nodiscard]] bool empty() const { return cardinality == 0; }

   /// The bytes of the viewed payload, in the representation `fromPayload` reads back. Unlike
   /// roaring's serialization formats, it holds no header (e.g. no run count), so the type and
   /// cardinality need to be stored alongside.
   [[nodiscard]] std::span<const std::byte> payload() const;

   [[nodiscard]] bool contains(uint16_t value) const {
      const detail::ContainerHeader container = header();
      return roaring::internal::container_contains(container.get(), value, typecode);
   }

   [[nodiscard]] size_t sizeInBytes() const;

   /// A deep copy of the viewed container.
   [[nodiscard]] RoaringContainer toOwning() const;

   friend uint32_t andCardinality(RoaringContainerView lhs, RoaringContainerView rhs);

   /// Forward iterator over the low-16-bit values held by the container, in ascending order. The
   /// iterator holds its own copy of the container header, so it may outlive the view, but the
   /// viewed container must outlive it and must not be mutated while an iteration is in progress.
   class ConstIterator {
      detail::ContainerHeader container;
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
      explicit ConstIterator(const detail::ContainerHeader& container)
          : container(container),
            exhausted(false) {
         internal_iterator = roaring::internal::container_init_iterator(
            this->container.get(), this->container.getTypecode(), &current_value
         );
      }

      uint16_t operator*() const { return current_value; }

      ConstIterator& operator++() {
         exhausted = !roaring::internal::container_iterator_next(
            container.get(), container.getTypecode(), &internal_iterator, &current_value
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
         return container.payload() == other.container.payload() &&
                current_value == other.current_value;
      }

      bool operator!=(const ConstIterator& other) const { return !(*this == other); }
   };

   [[nodiscard]] ConstIterator begin() const {
      // An empty container has no first value for `container_init_iterator` to load, so an empty
      // view iterates as an already-exhausted range.
      if (empty()) {
         return ConstIterator{};
      }
      return ConstIterator{header()};
   }

   [[nodiscard]] static ConstIterator end() { return ConstIterator{}; }
};

// On 64-bit systems we expect a 16 byte struct, on 32-bit a 12 byte struct
static_assert(sizeof(RoaringContainerView) == (sizeof(void*) == 8 ? 16 : 12));

/// Cardinality of the intersection, without materializing it.
[[nodiscard]] inline uint32_t andCardinality(RoaringContainerView lhs, RoaringContainerView rhs) {
   if (lhs.empty() || rhs.empty()) {
      return 0;
   }
   const detail::ContainerHeader lhs_container = lhs.header();
   const detail::ContainerHeader rhs_container = rhs.header();
   return static_cast<uint32_t>(roaring::internal::container_and_cardinality(
      lhs_container.get(), lhs.typecode, rhs_container.get(), rhs.typecode
   ));
}

[[nodiscard]] RoaringContainer operator&(RoaringContainerView lhs, RoaringContainerView rhs);
[[nodiscard]] RoaringContainer operator-(RoaringContainerView lhs, RoaringContainerView rhs);
[[nodiscard]] RoaringContainer operator|(RoaringContainerView lhs, RoaringContainerView rhs);

/// The values in `[0, universe_size)` that `view` does not hold, with `universe_size <= 2^16`.
/// `view` must not hold values outside of that range.
[[nodiscard]] RoaringContainer complement(RoaringContainerView view, uint32_t universe_size);

/// Union of any number of containers. Like roaring's `or_many`, more than two containers are OR-ed
/// lazily -- without maintaining the cardinality after every step -- into a bitset, which is
/// repaired (and converted to its most compact representation) once at the end.
[[nodiscard]] RoaringContainer fastUnion(std::span<const RoaringContainerView> views);

/// Views the containers of `bitmap` as `{key, view}` pairs in ascending key order. A shared
/// (copy-on-write) container is viewed through to the container it wraps. The views are
/// invalidated when `bitmap` is destroyed or mutated.
[[nodiscard]] std::vector<std::pair<uint16_t, RoaringContainerView>> containerViews(
   const roaring::Roaring& bitmap
);

/// Moves the containers out of `bitmap` as `{key, container}` pairs in ascending key order,
/// leaving `bitmap` empty. A shared (copy-on-write) container is replaced by a private copy.
[[nodiscard]] std::vector<std::pair<uint16_t, RoaringContainer>> releaseContainers(
   roaring::Roaring&& bitmap
);

/// Hands `container` over to `bitmap` as the container of the 2^16 block `key`, which must be
/// greater than every key `bitmap` already holds. An empty container is dropped, preserving
/// roaring's invariant that a bitmap holds no empty containers.
void appendTo(roaring::Roaring& bitmap, uint16_t key, RoaringContainer&& container);

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
