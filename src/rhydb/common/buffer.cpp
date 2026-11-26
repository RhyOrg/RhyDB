#include "rhydb/common/buffer.h"

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace rhydb::common {

void AlignedDelete::operator()(std::byte* pointer) const {
   ::operator delete[](pointer, std::align_val_t{Buffer::ALIGNMENT});
}

BufferBuilder::BufferBuilder(size_t size)
    : memory(new (std::align_val_t{Buffer::ALIGNMENT}) std::byte[size]()),
      size(size) {}

Buffer BufferBuilder::finish() && {
   return Buffer{std::move(memory), size};
}

}  // namespace rhydb::common
