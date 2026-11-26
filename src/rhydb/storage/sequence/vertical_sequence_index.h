#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "rhydb/common/buffer.h"
#include "rhydb/roaring_util/roaring_container.h"

namespace rhydb::storage::sequence {

/// One stored container of a `VerticalSequenceIndex`: the rows of its chunk that carry `symbol` at
/// `position`, as a roaring container whose payload lives in the index's payload section.
struct VerticalSequenceIndexEntry {
   uint32_t position;
   uint32_t cardinality;
   /// Offset of the payload from the start of the payload section.
   uint64_t payload_offset;
   uint32_t payload_size;
   uint8_t symbol;
   roaring_util::ContainerType type;
   uint16_t reserved;
};
static_assert(sizeof(VerticalSequenceIndexEntry) == 24);

struct VerticalSequenceIndexHeader {
   static constexpr uint32_t MAGIC = 0x53495652;  // "RVIS" in little-endian
   static constexpr uint32_t VERSION = 1;

   uint32_t magic = MAGIC;
   uint32_t version = VERSION;
   uint64_t num_entries = 0;
   /// Offset of the payload section from the start of the index.
   uint64_t payload_offset = 0;
   uint64_t payload_size = 0;
};
static_assert(sizeof(VerticalSequenceIndexHeader) == 32);

/// The vertical sequence index of a single 2^16-row ingestion chunk: for every position, the rows
/// of the chunk whose symbol differs from the local reference, one roaring container per symbol.
/// See documentation/developer/sequence_storage.md.
///
/// It is stored as one immutable, position-independent binary blob:
///
///    VerticalSequenceIndexHeader
///    VerticalSequenceIndexEntry[num_entries]   sorted by (position, symbol)
///    payload section                           64-byte aligned
///
/// The payload section holds every container's data in roaring's own in-memory representation
/// (bitset words, array values or runs), each bitset 64-byte aligned and every other payload
/// 8-byte aligned. Values are in native byte order.
///
/// Since nothing in the blob is a pointer, loading an index is a plain read of its bytes into an
/// aligned `common::Buffer` (or, later, a memory mapping of them): the containers are viewed in
/// place through `RoaringContainerView::fromPayload` and never materialized. Symbols are stored as
/// their `uint8_t` value; the index does not know the alphabet.
///
/// `VerticalSequenceIndex` itself is a non-owning view of such a blob, like
/// `RoaringContainerView`: whoever creates it keeps the buffer alive for as long as the view is
/// used (`SequenceChunk` holds both).
class VerticalSequenceIndex {
   std::span<const VerticalSequenceIndexEntry> entries_;
   std::span<const std::byte> payload;

   VerticalSequenceIndex(
      std::span<const VerticalSequenceIndexEntry> entries,
      std::span<const std::byte> payload
   )
       : entries_(entries),
         payload(payload) {}

  public:
   static constexpr size_t ALIGNMENT = common::Buffer::ALIGNMENT;

   /// Views `buffer` as an index. Checks the header, that every entry lies within the blob with a
   /// well-formed, aligned payload, and that the entries are sorted; it never touches the payload
   /// itself. Throws `std::runtime_error` if `buffer` does not hold a valid index.
   [[nodiscard]] static VerticalSequenceIndex fromBuffer(const common::Buffer& buffer);

   /// A view must not outlive its buffer.
   static VerticalSequenceIndex fromBuffer(const common::Buffer&& buffer) = delete;

   [[nodiscard]] std::span<const VerticalSequenceIndexEntry> entries() const { return entries_; }

   /// The entries at `position`, ordered by symbol.
   [[nodiscard]] std::span<const VerticalSequenceIndexEntry> entriesAtPosition(
      uint32_t position
   ) const;

   [[nodiscard]] roaring_util::RoaringContainerView container(
      const VerticalSequenceIndexEntry& entry
   ) const {
      return roaring_util::RoaringContainerView::fromPayload(
         entry.type, entry.cardinality, payload.subspan(entry.payload_offset, entry.payload_size)
      );
   }

   /// Bytes taken up by the stored containers' payloads.
   [[nodiscard]] size_t payloadSizeInBytes() const;

   /// The stored containers' size as roaring reports it (`container_size_in_bytes`).
   [[nodiscard]] size_t containersSizeInBytes() const;

   /// The containers to store at `position` in place of the current ones, in ascending symbol
   /// order.
   struct PositionReplacement {
      uint32_t position;
      std::vector<std::pair<uint8_t, roaring_util::RoaringContainerView>> containers;
   };

   /// The blob of a copy of this index in which the containers at each replaced position are
   /// exactly the given ones. `replacements` must be in ascending position order.
   [[nodiscard]] common::Buffer withPositionsReplaced(
      std::span<const PositionReplacement> replacements
   ) const;
};

/// Writes a `VerticalSequenceIndex`. Containers must be added in ascending (position, symbol)
/// order.
class VerticalSequenceIndexBuilder {
   std::vector<VerticalSequenceIndexEntry> entries;
   std::vector<std::byte> payload;

  public:
   /// Adds the container of the rows carrying `symbol` at `position`, in the representation it is
   /// given. An empty container is skipped, as the index only stores non-empty containers.
   void add(uint32_t position, uint8_t symbol, roaring_util::RoaringContainerView container);

   /// Adds the container holding the sorted, distinct in-chunk rows `rows`, in its most compact
   /// representation.
   void add(uint32_t position, uint8_t symbol, std::span<const uint16_t> rows);

   /// The index's blob, to be viewed with `VerticalSequenceIndex::fromBuffer`.
   [[nodiscard]] common::Buffer finish() &&;
};

}  // namespace rhydb::storage::sequence
