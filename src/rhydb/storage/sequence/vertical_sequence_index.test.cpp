#include "rhydb/storage/sequence/vertical_sequence_index.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using rhydb::roaring_util::RoaringContainerView;
using rhydb::storage::sequence::VerticalSequenceIndex;
using rhydb::storage::sequence::VerticalSequenceIndexBuilder;
using rhydb::storage::sequence::VerticalSequenceIndexEntry;
using rhydb::storage::sequence::VerticalSequenceIndexHeader;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;

namespace {

std::vector<uint16_t> valuesOf(RoaringContainerView view) {
   return {view.begin(), RoaringContainerView::end()};
}

std::vector<uint16_t> range(uint16_t begin, uint16_t end, uint16_t step = 1) {
   std::vector<uint16_t> result;
   for (uint32_t value = begin; value < end; value += step) {
      result.push_back(static_cast<uint16_t>(value));
   }
   return result;
}

// An array container at (1, 2) and (1, 5), a bitset container at (3, 0) and a run container at
// (7, 1).
VerticalSequenceIndex exampleIndex() {
   VerticalSequenceIndexBuilder builder;
   const std::vector<uint16_t> array_rows{3, 9, 27};
   const std::vector<uint16_t> other_array_rows{4};
   const auto bitset_rows = range(0, 20000, 3);
   const auto run_rows = range(100, 5000);
   builder.add(1, 2, array_rows);
   builder.add(1, 5, other_array_rows);
   builder.add(3, 0, bitset_rows);
   builder.add(7, 1, run_rows);
   return std::move(builder).finish();
}

// A copy of `index`'s bytes in freshly allocated memory, as it would be after reading it back
// from disk.
VerticalSequenceIndex reread(const VerticalSequenceIndex& index) {
   const auto bytes = index.data();
   auto memory = VerticalSequenceIndex::allocate(bytes.size());
   std::memcpy(memory.get(), bytes.data(), bytes.size());
   const std::span<const std::byte> copy{memory.get(), bytes.size()};
   return VerticalSequenceIndex::fromBytes(std::move(memory), copy);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void expectExampleContent(const VerticalSequenceIndex& index) {
   ASSERT_EQ(index.entries().size(), 4);

   const auto at_one = index.entriesAtPosition(1);
   ASSERT_EQ(at_one.size(), 2);
   EXPECT_EQ(at_one[0].symbol, 2);
   EXPECT_EQ(at_one[0].type, rhydb::roaring_util::ContainerType::ARRAY);
   EXPECT_THAT(valuesOf(index.container(at_one[0])), ElementsAre(3, 9, 27));
   EXPECT_EQ(at_one[1].symbol, 5);
   EXPECT_THAT(valuesOf(index.container(at_one[1])), ElementsAre(4));

   const auto at_three = index.entriesAtPosition(3);
   ASSERT_EQ(at_three.size(), 1);
   EXPECT_EQ(at_three[0].type, rhydb::roaring_util::ContainerType::BITSET);
   EXPECT_EQ(index.container(at_three[0]).getCardinality(), range(0, 20000, 3).size());
   EXPECT_THAT(valuesOf(index.container(at_three[0])), ElementsAreArray(range(0, 20000, 3)));

   const auto at_seven = index.entriesAtPosition(7);
   ASSERT_EQ(at_seven.size(), 1);
   EXPECT_EQ(at_seven[0].type, rhydb::roaring_util::ContainerType::RUN);
   EXPECT_EQ(index.container(at_seven[0]).getCardinality(), 4900);
   EXPECT_THAT(valuesOf(index.container(at_seven[0])), ElementsAreArray(range(100, 5000)));

   EXPECT_TRUE(index.entriesAtPosition(0).empty());
   EXPECT_TRUE(index.entriesAtPosition(2).empty());
   EXPECT_TRUE(index.entriesAtPosition(8).empty());
}

VerticalSequenceIndexHeader headerOf(const std::vector<std::byte>& bytes) {
   VerticalSequenceIndexHeader header;
   std::memcpy(&header, bytes.data(), sizeof(header));
   return header;
}

VerticalSequenceIndexEntry* entryOf(std::span<std::byte> bytes, size_t index) {
   return reinterpret_cast<VerticalSequenceIndexEntry*>(  // NOLINT(*-reinterpret-cast)
      bytes.data() + sizeof(VerticalSequenceIndexHeader) + (index * sizeof(VerticalSequenceIndexEntry))
   );
}

// Re-reads `index` after applying `corrupt` to a copy of its bytes.
template <typename Corrupt>
void rereadCorrupted(const VerticalSequenceIndex& index, Corrupt corrupt) {
   const auto bytes = index.data();
   auto memory = VerticalSequenceIndex::allocate(bytes.size());
   std::memcpy(memory.get(), bytes.data(), bytes.size());
   corrupt(std::span<std::byte>{memory.get(), bytes.size()});
   const std::span<const std::byte> copy{memory.get(), bytes.size()};
   static_cast<void>(VerticalSequenceIndex::fromBytes(std::move(memory), copy));
}

}  // namespace

TEST(VerticalSequenceIndex, defaultIndexIsEmptyAndRereadable) {
   const VerticalSequenceIndex index;
   EXPECT_TRUE(index.entries().empty());
   EXPECT_TRUE(index.entriesAtPosition(0).empty());
   EXPECT_EQ(index.payloadSizeInBytes(), 0);
   EXPECT_TRUE(reread(index).entries().empty());
}

TEST(VerticalSequenceIndex, storesEveryContainerTypeAndFindsThemByPosition) {
   expectExampleContent(exampleIndex());
}

TEST(VerticalSequenceIndex, rereadBytesViewTheSameContainersInPlace) {
   const auto original = exampleIndex();
   const auto copy = reread(original);
   expectExampleContent(copy);

   // The containers are views into the new bytes, not into the original's.
   const auto entry = copy.entriesAtPosition(3)[0];
   const auto* payload = copy.container(entry).payload().data();
   EXPECT_GE(payload, copy.data().data());
   EXPECT_LT(payload, copy.data().data() + copy.data().size());
   EXPECT_EQ(reinterpret_cast<uintptr_t>(payload) % VerticalSequenceIndex::ALIGNMENT, 0);  // NOLINT
}

TEST(VerticalSequenceIndex, builderSkipsEmptyContainers) {
   VerticalSequenceIndexBuilder builder;
   builder.add(0, 0, std::span<const uint16_t>{});
   const auto index = std::move(builder).finish();
   EXPECT_TRUE(index.entries().empty());
}

TEST(VerticalSequenceIndex, rejectsMalformedBytes) {
   const auto index = exampleIndex();
   auto expect_rejected = [&](auto corrupt, std::string_view message) {
      EXPECT_THAT(
         [&]() { rereadCorrupted(index, corrupt); },
         ThrowsMessage<std::runtime_error>(HasSubstr(std::string{message}))
      );
   };

   expect_rejected([](std::span<std::byte> bytes) { bytes[0] = std::byte{0}; }, "magic");
   expect_rejected(
      [](std::span<std::byte> bytes) {
         VerticalSequenceIndexHeader header;
         std::memcpy(&header, bytes.data(), sizeof(header));
         header.num_entries = 1'000'000;
         std::memcpy(bytes.data(), &header, sizeof(header));
      },
      "do not fit"
   );
   expect_rejected(
      [](std::span<std::byte> bytes) { entryOf(bytes, 2)->payload_offset += 8; },
      "outside the payload section"
   );
   expect_rejected(
      [](std::span<std::byte> bytes) { entryOf(bytes, 0)->payload_offset += 1; },
      "misaligned payload"
   );
   expect_rejected(
      [](std::span<std::byte> bytes) { entryOf(bytes, 0)->cardinality = 5; },
      "payload size does not match its cardinality"
   );
   expect_rejected(
      [](std::span<std::byte> bytes) {
         entryOf(bytes, 1)->type = static_cast<rhydb::roaring_util::ContainerType>(9);
      },
      "unknown container"
   );
   expect_rejected(
      [](std::span<std::byte> bytes) { entryOf(bytes, 1)->symbol = 1; }, "not strictly sorted"
   );

   EXPECT_THAT(
      [&]() {
         const auto bytes = index.data();
         static_cast<void>(VerticalSequenceIndex::fromBytes(nullptr, bytes.subspan(1)));
      },
      ThrowsMessage<std::runtime_error>(HasSubstr("aligned"))
   );
}

TEST(VerticalSequenceIndex, findsTheEntriesOfEveryPositionOfAnUnevenlyFilledIndex) {
   // Positions with one, several or no entries, crowded at the start and sparse at the end
   VerticalSequenceIndexBuilder builder;
   std::vector<std::vector<uint8_t>> symbols_at(1000);
   for (uint32_t position = 0; position < 1000; ++position) {
      uint8_t num_symbols = 0;
      if (position < 100) {
         num_symbols = position % 7;
      } else if (position % 97 == 0) {
         num_symbols = 2;
      }
      for (uint8_t symbol = 0; symbol < num_symbols; ++symbol) {
         const std::vector<uint16_t> rows{static_cast<uint16_t>(position)};
         builder.add(position, symbol, rows);
         symbols_at[position].push_back(symbol);
      }
   }
   const auto index = std::move(builder).finish();
   for (uint32_t position = 0; position < 1100; ++position) {
      std::vector<uint8_t> found;
      for (const auto& entry : index.entriesAtPosition(position)) {
         EXPECT_EQ(entry.position, position);
         found.push_back(entry.symbol);
      }
      EXPECT_EQ(found, position < 1000 ? symbols_at[position] : std::vector<uint8_t>{})
         << "at position " << position;
   }
}
