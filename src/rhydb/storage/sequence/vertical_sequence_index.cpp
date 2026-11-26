#include "rhydb/storage/sequence/vertical_sequence_index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <fmt/format.h>
#include <roaring/roaring.hh>

#include "rhydb/common/panic.h"

namespace rhydb::storage::sequence {

namespace {

using roaring_util::ContainerType;

constexpr size_t BITSET_PAYLOAD_SIZE = roaring_util::BITSET_CONTAINER_WORDS * sizeof(uint64_t);
/// A run is a `{value, length - 1}` pair of `uint16_t`.
constexpr size_t RUN_SIZE = 2 * sizeof(uint16_t);
constexpr uint32_t MAX_CARDINALITY = 1U << 16;

constexpr size_t alignUp(size_t value, size_t alignment) {
   return (value + alignment - 1) / alignment * alignment;
}

/// The alignment a container payload is written at within the payload section.
constexpr size_t payloadAlignment(ContainerType type) {
   return type == ContainerType::BITSET ? VerticalSequenceIndex::ALIGNMENT : 8;
}

void checkEntry(const VerticalSequenceIndexEntry& entry, uint64_t payload_size, size_t index) {
   auto fail = [&](std::string_view reason) {
      throw std::runtime_error(
         fmt::format(
            "invalid vertical sequence index: entry {} (position {}, symbol {}) {}",
            index,
            entry.position,
            entry.symbol,
            reason
         )
      );
   };
   if (entry.cardinality == 0 || entry.cardinality > MAX_CARDINALITY) {
      fail(fmt::format("has cardinality {}", entry.cardinality));
   }
   if (
      entry.payload_offset > payload_size ||
      entry.payload_size > payload_size - entry.payload_offset
   ) {
      fail("has a payload outside the payload section");
   }
   switch (entry.type) {
      case ContainerType::BITSET:
         if (entry.payload_size != BITSET_PAYLOAD_SIZE) {
            fail("is a bitset container with a payload of the wrong size");
         }
         break;
      case ContainerType::ARRAY:
         if (entry.payload_size != entry.cardinality * sizeof(uint16_t)) {
            fail("is an array container whose payload size does not match its cardinality");
         }
         break;
      case ContainerType::RUN:
         if (entry.payload_size == 0 || entry.payload_size % RUN_SIZE != 0) {
            fail("is a run container with a payload that is not a whole number of runs");
         }
         break;
      default:
         fail(fmt::format("has the unknown container type {}", static_cast<uint32_t>(entry.type)));
   }
   if (entry.payload_offset % payloadAlignment(entry.type) != 0) {
      fail("has a misaligned payload");
   }
}

}  // namespace

VerticalSequenceIndex::VerticalSequenceIndex() {
   *this = VerticalSequenceIndexBuilder{}.finish();
}

// NOLINTNEXTLINE(modernize-avoid-c-arrays)
std::shared_ptr<std::byte[]> VerticalSequenceIndex::allocate(size_t size) {
   auto* memory = new (std::align_val_t{ALIGNMENT}) std::byte[size]();
   return {memory, [](std::byte* pointer) {
              ::operator delete[](pointer, std::align_val_t{ALIGNMENT});
           }};
}

VerticalSequenceIndex VerticalSequenceIndex::fromBytes(
   std::shared_ptr<const void> owner,
   std::span<const std::byte> bytes
) {
   auto fail = [](std::string_view reason) {
      throw std::runtime_error(fmt::format("invalid vertical sequence index: {}", reason));
   };
   VerticalSequenceIndexHeader header;
   if (bytes.size() < sizeof(header)) {
      fail(fmt::format("{} bytes cannot hold a header", bytes.size()));
   }
   if (reinterpret_cast<uintptr_t>(bytes.data()) % ALIGNMENT != 0) {  // NOLINT(*-reinterpret-cast)
      fail(fmt::format("the bytes are not {}-byte aligned", ALIGNMENT));
   }
   std::memcpy(&header, bytes.data(), sizeof(header));
   if (header.magic != VerticalSequenceIndexHeader::MAGIC) {
      fail("wrong magic number");
   }
   if (header.version != VerticalSequenceIndexHeader::VERSION) {
      fail(
         fmt::format(
            "version {}, expected {}", header.version, VerticalSequenceIndexHeader::VERSION
         )
      );
   }
   const size_t max_entries = (bytes.size() - sizeof(header)) / sizeof(VerticalSequenceIndexEntry);
   if (header.num_entries > max_entries) {
      fail(fmt::format("{} entries do not fit into {} bytes", header.num_entries, bytes.size()));
   }
   const size_t entries_end =
      sizeof(header) + (header.num_entries * sizeof(VerticalSequenceIndexEntry));
   if (
      header.payload_offset < entries_end || header.payload_offset % ALIGNMENT != 0 ||
      header.payload_offset > bytes.size() ||
      header.payload_size != bytes.size() - header.payload_offset
   ) {
      fail(
         fmt::format(
            "payload section [{}, +{}) does not follow the entries and end the {} bytes",
            header.payload_offset,
            header.payload_size,
            bytes.size()
         )
      );
   }

   const std::span<const VerticalSequenceIndexEntry> entries{
      reinterpret_cast<const VerticalSequenceIndexEntry*>(bytes.data() + sizeof(header)),  // NOLINT
      header.num_entries
   };
   for (size_t index = 0; index < entries.size(); ++index) {
      checkEntry(entries[index], header.payload_size, index);
      if (
         index > 0 && std::tie(entries[index - 1].position, entries[index - 1].symbol) >=
                         std::tie(entries[index].position, entries[index].symbol)
      ) {
         fail(fmt::format("entries are not strictly sorted at entry {}", index));
      }
   }
   return VerticalSequenceIndex{
      std::move(owner), bytes, entries, bytes.data() + header.payload_offset
   };
}

std::span<const VerticalSequenceIndexEntry> VerticalSequenceIndex::entriesAtPosition(
   uint32_t position
) const {
   const auto begin =
      std::ranges::lower_bound(entries_, position, {}, &VerticalSequenceIndexEntry::position);
   // A position has at most one entry per symbol, so the end is found by stepping forward
   auto end = begin;
   while (end != entries_.end() && end->position == position) {
      ++end;
   }
   return {begin, end};
}

size_t VerticalSequenceIndex::payloadSizeInBytes() const {
   size_t result = 0;
   for (const auto& entry : entries_) {
      result += entry.payload_size;
   }
   return result;
}

size_t VerticalSequenceIndex::containersSizeInBytes() const {
   size_t result = 0;
   for (const auto& entry : entries_) {
      result += container(entry).sizeInBytes();
   }
   return result;
}

VerticalSequenceIndex VerticalSequenceIndex::withPositionsReplaced(
   std::span<const PositionReplacement> replacements
) const {
   VerticalSequenceIndexBuilder builder;
   size_t entry_idx = 0;
   auto copy_entries_before = [&](uint32_t position) {
      for (; entry_idx < entries_.size() && entries_[entry_idx].position < position; ++entry_idx) {
         const auto& entry = entries_[entry_idx];
         builder.add(entry.position, entry.symbol, container(entry));
      }
   };
   for (const auto& [position, containers] : replacements) {
      copy_entries_before(position);
      while (entry_idx < entries_.size() && entries_[entry_idx].position == position) {
         ++entry_idx;
      }
      for (const auto& [symbol, replacement] : containers) {
         builder.add(position, symbol, replacement);
      }
   }
   for (; entry_idx < entries_.size(); ++entry_idx) {
      const auto& entry = entries_[entry_idx];
      builder.add(entry.position, entry.symbol, container(entry));
   }
   return std::move(builder).finish();
}

void VerticalSequenceIndexBuilder::add(
   uint32_t position,
   uint8_t symbol,
   roaring_util::RoaringContainerView container
) {
   if (container.empty()) {
      return;
   }
   RHYDB_ASSERT(
      entries.empty() ||
      std::tie(entries.back().position, entries.back().symbol) < std::tie(position, symbol)
   );
   const std::span<const std::byte> container_payload = container.payload();
   const size_t offset = alignUp(payload.size(), payloadAlignment(container.getType()));
   payload.resize(offset + container_payload.size());
   std::ranges::copy(container_payload, payload.begin() + static_cast<ptrdiff_t>(offset));
   entries.push_back(
      VerticalSequenceIndexEntry{
         .position = position,
         .cardinality = container.getCardinality(),
         .payload_offset = offset,
         .payload_size = static_cast<uint32_t>(container_payload.size()),
         .symbol = symbol,
         .type = container.getType(),
         .reserved = 0,
      }
   );
}

void VerticalSequenceIndexBuilder::add(
   uint32_t position,
   uint8_t symbol,
   std::span<const uint16_t> rows
) {
   if (rows.empty()) {
      return;
   }
   auto container = roaring_util::RoaringContainer::withCapacity(static_cast<int32_t>(rows.size()));
   for (const uint16_t row : rows) {
      container.add(row);
   }
   container.runOptimizeAndShrink();
   add(position, symbol, roaring_util::RoaringContainerView{container});
}

VerticalSequenceIndex VerticalSequenceIndexBuilder::finish() && {
   VerticalSequenceIndexHeader header;
   header.num_entries = entries.size();
   header.payload_offset = alignUp(
      sizeof(header) + (entries.size() * sizeof(VerticalSequenceIndexEntry)),
      VerticalSequenceIndex::ALIGNMENT
   );
   header.payload_size = payload.size();

   const size_t size = header.payload_offset + header.payload_size;
   // NOLINTNEXTLINE(modernize-avoid-c-arrays)
   std::shared_ptr<std::byte[]> memory = VerticalSequenceIndex::allocate(size);
   std::memcpy(memory.get(), &header, sizeof(header));
   if (!entries.empty()) {
      std::memcpy(
         memory.get() + sizeof(header),
         entries.data(),
         entries.size() * sizeof(VerticalSequenceIndexEntry)
      );
   }
   if (!payload.empty()) {
      std::memcpy(memory.get() + header.payload_offset, payload.data(), payload.size());
   }
   const std::span<const std::byte> bytes{memory.get(), size};
   return VerticalSequenceIndex::fromBytes(std::move(memory), bytes);
}

}  // namespace rhydb::storage::sequence
