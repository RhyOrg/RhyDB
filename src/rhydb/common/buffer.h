#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <boost/serialization/access.hpp>
#include <boost/serialization/split_member.hpp>

#include "rhydb/common/panic.h"

namespace rhydb::common {

/// Frees memory allocated with `std::align_val_t{Buffer::ALIGNMENT}`.
struct AlignedDelete {
   void operator()(std::byte* pointer) const;
};

/// An immutable run of bytes starting at a `ALIGNMENT`-byte aligned address, which owns them.
///
/// A buffer is move-only. Moving it does not move its bytes, so views into them stay valid for as
/// long as some buffer owns them. Whoever shares a buffer holds it in a
/// `std::shared_ptr<const Buffer>`.
///
/// It is created by a `BufferBuilder`, or read back as an archive wrote it (`archive << buffer`
/// writes its size followed by its raw bytes, `archive >> buffer` reads them into new memory).
/// Where the bytes live is an implementation detail, so that e.g. a memory-mapped buffer can be
/// added without changing its users.
class Buffer {
  public:
   /// The alignment of the first byte: a cache line, enough for every scalar and SIMD type.
   static constexpr size_t ALIGNMENT = 64;

  private:
   friend class BufferBuilder;

   std::unique_ptr<std::byte[], AlignedDelete> memory;  // NOLINT(modernize-avoid-c-arrays)
   size_t size_ = 0;

   // NOLINTNEXTLINE(modernize-avoid-c-arrays)
   Buffer(std::unique_ptr<std::byte[], AlignedDelete> memory, size_t size)
       : memory(std::move(memory)),
         size_(size) {}

  public:
   /// An empty buffer.
   Buffer() = default;

   Buffer(const Buffer&) = delete;
   Buffer& operator=(const Buffer&) = delete;
   /// Leaves `other` empty.
   Buffer(Buffer&& other) noexcept
       : memory(std::move(other.memory)),
         size_(std::exchange(other.size_, 0)) {}

   /// Leaves `other` empty.
   Buffer& operator=(Buffer&& other) noexcept {
      memory = std::move(other.memory);
      size_ = std::exchange(other.size_, 0);
      return *this;
   }

   ~Buffer() = default;

   [[nodiscard]] std::span<const std::byte> bytes() const { return {memory.get(), size_}; }

   [[nodiscard]] size_t size() const { return size_; }

   /// The `count` values of type `T` starting `offset` bytes into the buffer, viewed in place.
   /// Panics unless they lie within the buffer and `offset` is aligned for `T`.
   template <typename T>
   [[nodiscard]] std::span<const T> view(size_t offset, size_t count) const {
      static_assert(std::is_trivially_copyable_v<T>);
      static_assert(ALIGNMENT % alignof(T) == 0);
      RHYDB_ASSERT_EQ(offset % alignof(T), size_t{0});
      RHYDB_ASSERT_LE(offset, size_);
      RHYDB_ASSERT_LE(count, (size_ - offset) / sizeof(T));
      if (count == 0) {
         return {};
      }
      const auto* values = reinterpret_cast<const T*>(memory.get() + offset);  // NOLINT(*-cast)
      return {values, count};
   }

   /// Reads `size` bytes from `archive` (`archive.load_binary`) into a new buffer.
   template <class Archive>
   [[nodiscard]] static Buffer read(Archive& archive, size_t size);

  private:
   friend class boost::serialization::access;

   template <class Archive>
   void save(Archive& archive, [[maybe_unused]] const uint32_t version) const {
      const uint64_t size = size_;
      archive << size;
      archive.save_binary(memory.get(), size_);
   }

   template <class Archive>
   void load(Archive& archive, [[maybe_unused]] const uint32_t version) {
      uint64_t size = 0;
      archive >> size;
      *this = read(archive, size);
   }

   BOOST_SERIALIZATION_SPLIT_MEMBER()
};

/// Creates a `Buffer`: allocates its aligned, zeroed bytes, lets them be written, and hands them
/// over to the buffer, which never changes them again.
class BufferBuilder {
   std::unique_ptr<std::byte[], AlignedDelete> memory;  // NOLINT(modernize-avoid-c-arrays)
   size_t size;

  public:
   explicit BufferBuilder(size_t size);

   [[nodiscard]] std::span<std::byte> bytes() { return {memory.get(), size}; }

   /// Copies `values` to `offset` bytes into the buffer. Panics unless they fit into it.
   template <typename T, size_t EXTENT>
   void write(size_t offset, std::span<T, EXTENT> values) {
      static_assert(std::is_trivially_copyable_v<T>);
      RHYDB_ASSERT_LE(offset, size);
      RHYDB_ASSERT_LE(values.size_bytes(), size - offset);
      if (!values.empty()) {
         std::memcpy(memory.get() + offset, values.data(), values.size_bytes());
      }
   }

   [[nodiscard]] Buffer finish() &&;
};

template <class Archive>
Buffer Buffer::read(Archive& archive, size_t size) {
   BufferBuilder builder{size};
   if (size > 0) {
      archive.load_binary(builder.bytes().data(), size);
   }
   return std::move(builder).finish();
}

}  // namespace rhydb::common
