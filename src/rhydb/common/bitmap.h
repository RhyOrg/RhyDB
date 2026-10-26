#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <roaring/roaring.hh>

#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb {

/// A full row-id set represented as a sorted list of 2^16-keyed containers -- the same
/// decomposition a `roaring::Roaring` uses internally. Each container is either a non-owning
/// `RoaringContainerView` into a container owned elsewhere (a column index, or another bitmap that
/// outlives this one) or an owning `RoaringContainer`. A bitmap constructed from an existing
/// `roaring::Roaring` starts as all views; the first set operation that has to change a container
/// replaces that view with a private owning copy -- copy-on-write, at the granularity of a single
/// container.
///
/// Set-algebra (AND, OR, ANDNOT and the matching cardinalities) is computed directly on these
/// container lists through the roaring container-level C API
class Bitmap {
   /// A single 2^16 block's container, either a non-owning view or a privately-owned copy.
   using Container =
      std::variant<roaring_util::RoaringContainerView, roaring_util::RoaringContainer>;

   /// Struct-of-arrays: `keys[i]` is the 2^16 block key of the container held in `containers[i]`,
   /// so the two vectors always have the same length. `keys` is sorted strictly ascending and no
   /// container is ever empty
   std::vector<uint16_t> keys;
   std::vector<Container> containers;

   /// read-only handle onto a container, regardless of ownership.
   static roaring_util::RoaringContainerView viewOf(const Container& container);

   /// Deep-copies a container: view alternatives are copied as views, owning alternatives are
   /// cloned into independent owning containers.
   static Container copyContainer(const Container& container);

   /// Appends a freshly produced container (and its key) to the parallel output arrays, taking
   /// ownership -- or frees it if empty, preserving roaring's "no empty containers" invariant.
   static void pushIfNonEmpty(
      std::vector<uint16_t>& out_keys,
      std::vector<Container>& out_containers,
      uint16_t key,
      roaring::internal::container_t* container,
      uint8_t typecode
   );

  public:
   Bitmap() = default;

   /// Views the containers of an externally owned bitmap. The pointee must outlive this object and
   /// must not be mutated while views onto it exist.
   explicit Bitmap(const roaring::Roaring* bitmap);

   /// Takes ownership of the containers of `bitmap`, leaving `bitmap` empty.
   explicit Bitmap(roaring::Roaring&& bitmap);

   Bitmap(const Bitmap& other);
   Bitmap& operator=(const Bitmap& other);
   Bitmap(Bitmap&&) noexcept = default;
   Bitmap& operator=(Bitmap&&) noexcept = default;
   ~Bitmap() = default;

   [[nodiscard]] uint64_t cardinality() const;

   [[nodiscard]] bool isEmpty() const;

   /// Forward iterator over the bitmap's containers in ascending key order. Dereferencing yields a
   /// `{key, view}` pair by value - the 2^16 block key (the high 16 bits of the row ids it holds)
   /// and a non-owning view of its container - without materializing any intermediate collection.
   /// Iterating the view in turn yields the low 16 bits of each contained row id
   class ConstIterator {
      const Bitmap* bitmap = nullptr;
      size_t index = 0;

     public:
      // The dereferenced pair is a value, not an lvalue in a container, so this is an input
      // iterator; it is nonetheless multi-pass (nothing is consumed) and safe to traverse
      // repeatedly.
      using iterator_category = std::input_iterator_tag;
      using value_type = std::pair<uint16_t, roaring_util::RoaringContainerView>;
      using difference_type = std::ptrdiff_t;
      using pointer = void;
      using reference = value_type;

      ConstIterator() = default;

      ConstIterator(const Bitmap* bitmap, size_t index)
          : bitmap(bitmap),
            index(index) {}

      value_type operator*() const {
         return {bitmap->keys[index], viewOf(bitmap->containers[index])};
      }

      ConstIterator& operator++() {
         ++index;
         return *this;
      }

      ConstIterator operator++(int) {
         ConstIterator copy = *this;
         ++index;
         return copy;
      }

      bool operator==(const ConstIterator& other) const { return index == other.index; }
      bool operator!=(const ConstIterator& other) const { return index != other.index; }
   };

   [[nodiscard]] ConstIterator begin() const { return ConstIterator{this, 0}; }
   [[nodiscard]] ConstIterator end() const { return ConstIterator{this, keys.size()}; }

   /// Cardinality of the intersection with `other`, without materializing it.
   [[nodiscard]] uint64_t andCardinality(const Bitmap& other) const;

   Bitmap& operator&=(const Bitmap& other);
   Bitmap& operator-=(const Bitmap& other);
   Bitmap& operator|=(const Bitmap& other);

   [[nodiscard]] Bitmap operator&(const Bitmap& other) const;
   [[nodiscard]] Bitmap operator-(const Bitmap& other) const;

   /// Union of many bitmaps, computed container-by-container in a single k-way merge.
   [[nodiscard]] static Bitmap fastUnion(const std::vector<Bitmap>& bitmaps);

   /// Builds a bitmap that *views* externally-owned containers (e.g. a column index's stored
   /// containers) rather than cloning them: a key with a single container becomes a zero-copy
   /// view, and keys shared by several containers are OR-ed into one owning container. The viewed
   /// containers must outlive the returned bitmap and must not be mutated while it exists. Input
   /// order does not matter.
   [[nodiscard]] static Bitmap fromContainerViews(
      std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>> container_views
   );

   /// Views per-chunk containers, where `chunk_containers[k]` holds the low 16 bits of the row ids
   /// in the 2^16 block `k` (e.g. a column's null bitmap). Empty containers are skipped. The
   /// containers must outlive the returned bitmap and must not be mutated while it exists.
   [[nodiscard]] static Bitmap fromChunkContainers(
      std::span<const roaring_util::RoaringContainer> chunk_containers
   );

   /// Builds a bitmap from one container per chunk: `chunk_container(k)` returns the low 16 bits of
   /// the row ids in the 2^16 block `k`, for `k < num_chunks`, either as a
   /// `RoaringContainerView` (whose container must outlive the returned bitmap and must not be
   /// mutated while it exists) or as an owning `RoaringContainer`. Empty containers are skipped.
   template <typename ChunkContainer>
   [[nodiscard]] static Bitmap fromChunks(size_t num_chunks, const ChunkContainer& chunk_container) {
      Bitmap result;
      for (size_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
         auto container = chunk_container(chunk_id);
         if (container.empty()) {
            continue;
         }
         result.keys.push_back(static_cast<uint16_t>(chunk_id));
         result.containers.emplace_back(std::move(container));
      }
      return result;
   }

   /// Materializes into a standalone `roaring::Roaring`. Intended for the end of a query only,
   /// where the result is handed to a consumer -- not for intermediate computation.
   [[nodiscard]] roaring::Roaring toRoaring() const;
};

}  // namespace rhydb
