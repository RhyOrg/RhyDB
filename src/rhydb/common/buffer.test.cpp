#include "rhydb/common/buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>

using rhydb::common::Buffer;
using rhydb::common::BufferBuilder;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

namespace {

bool isAligned(const void* pointer) {
   const auto address = reinterpret_cast<uintptr_t>(pointer);  // NOLINT(*-reinterpret-cast)
   return address % Buffer::ALIGNMENT == 0;
}

// A buffer holding the `uint32_t`s 1, 2, 3 at offset 0 and the `uint64_t` 42 at offset 16.
Buffer exampleBuffer() {
   BufferBuilder builder{24};
   const std::vector<uint32_t> values{1, 2, 3};
   const uint64_t value = 42;
   builder.write(0, std::span{values});
   builder.write(16, std::span{&value, 1});
   return std::move(builder).finish();
}

}  // namespace

TEST(Buffer, builderAllocatesAlignedZeroedBytes) {
   const Buffer empty;
   EXPECT_EQ(empty.size(), 0);
   EXPECT_TRUE(empty.view<uint64_t>(0, 0).empty());
   for (const size_t size : {0UL, 1UL, 63UL, 64UL, 1000UL}) {
      BufferBuilder builder{size};
      EXPECT_TRUE(isAligned(builder.bytes().data()));
      EXPECT_TRUE(std::ranges::all_of(builder.bytes(), [](std::byte byte) {
         return byte == std::byte{0};
      }));
      const Buffer buffer = std::move(builder).finish();
      EXPECT_EQ(buffer.size(), size);
      EXPECT_TRUE(isAligned(buffer.bytes().data()));
   }
}

TEST(Buffer, viewsTheWrittenValuesInPlaceAndKeepsThemWhenMoved) {
   Buffer buffer = exampleBuffer();
   const auto values = buffer.view<uint32_t>(0, 3);
   EXPECT_THAT(values, ElementsAre(1, 2, 3));
   EXPECT_EQ(static_cast<const void*>(values.data()), buffer.bytes().data());
   EXPECT_THAT(buffer.view<uint64_t>(16, 1), ElementsAre(42));
   EXPECT_TRUE(buffer.view<uint64_t>(24, 0).empty());

   Buffer moved = std::move(buffer);
   EXPECT_EQ(moved.view<uint32_t>(0, 3).data(), values.data());
   EXPECT_EQ(buffer.size(), 0);  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
   Buffer assigned;
   assigned = std::move(moved);
   EXPECT_EQ(assigned.view<uint32_t>(0, 3).data(), values.data());
   EXPECT_THAT(values, ElementsAre(1, 2, 3));
}

TEST(Buffer, viewAndWritePanicOutsideTheBufferOrWhenMisaligned) {
   const Buffer buffer = exampleBuffer();
   BufferBuilder builder{8};
   const std::vector<uint32_t> values{1, 2, 3};
   const std::vector<std::pair<std::function<void()>, std::string>> cases{
      {[&] { static_cast<void>(buffer.view<uint64_t>(16, 2)); }, "ASSERT_LE"},
      {[&] { static_cast<void>(buffer.view<uint32_t>(32, 0)); }, "ASSERT_LE"},
      {[&] { static_cast<void>(buffer.view<uint64_t>(4, 1)); }, "ASSERT_EQ"},
      {[&] { builder.write(0, std::span{values}); }, "ASSERT_LE"},
      {[&] { builder.write(9, std::span<const uint32_t>{}); }, "ASSERT_LE"},
   };
   for (const auto& [call, message] : cases) {
      EXPECT_THAT(call, ThrowsMessage<std::runtime_error>(HasSubstr(message)));
   }
}

TEST(Buffer, serializationRoundTripsTheBytesIntoAlignedMemory) {
   std::vector<Buffer> original;
   original.push_back(exampleBuffer());
   original.emplace_back();
   std::stringstream stream;
   {
      boost::archive::binary_oarchive output_archive(stream);
      output_archive << original;
   }
   std::vector<Buffer> loaded;
   {
      boost::archive::binary_iarchive input_archive(stream);
      input_archive >> loaded;
   }
   ASSERT_EQ(loaded.size(), 2);
   EXPECT_TRUE(std::ranges::equal(loaded[0].bytes(), original[0].bytes()));
   EXPECT_TRUE(isAligned(loaded[0].bytes().data()));
   EXPECT_THAT(loaded[0].view<uint32_t>(0, 3), ElementsAre(1, 2, 3));
   EXPECT_EQ(loaded[1].size(), 0);

   // `read` takes just the given number of raw bytes.
   std::stringstream raw_stream;
   {
      boost::archive::binary_oarchive output_archive(raw_stream);
      output_archive.save_binary(original[0].bytes().data(), original[0].size());
   }
   boost::archive::binary_iarchive input_archive(raw_stream);
   const Buffer read = Buffer::read(input_archive, original[0].size());
   EXPECT_TRUE(std::ranges::equal(read.bytes(), original[0].bytes()));
   EXPECT_TRUE(isAligned(read.bytes().data()));
}
