#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/common/bitmap.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::filter::operators {

enum Type : uint8_t {
   EMPTY,
   FULL,
   INDEX_SCAN,
   INTERSECTION,
   COMPLEMENT,
   RANGE_SELECTION,
   SELECTION,
   THRESHOLD,
   UNION,
   BITMAP_PRODUCER,
};

/// A node of a compiled filter, which selects a set of rows. It is evaluated chunk by chunk: chunk
/// `chunk_id` holds the rows with the global row ids `[chunk_id << 16, (chunk_id << 16) +
/// chunkSize(chunk_id))` (see `RowLayout`), which map onto a single roaring container.
class Operator {
  protected:
   storage::column::RowLayout row_layout;

  public:
   explicit Operator(storage::column::RowLayout row_layout);

   virtual ~Operator() noexcept;

   [[nodiscard]] virtual Type type() const = 0;

   /// The rows of chunk `chunk_id` that this operator selects, as the low 16 bits of their row ids.
   /// The result may borrow the containers of storage indexes, but never those of the operator
   /// itself, so it stays valid after the operator is destroyed.
   [[nodiscard]] virtual roaring_util::CopyOnWriteContainer evaluateChunk(
      uint16_t chunk_id
   ) const = 0;

   /// The rows of all chunks that this operator selects
   [[nodiscard]] Bitmap evaluate() const;

   [[nodiscard]] virtual std::string toString() const = 0;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Operator>&& some_operator);
};

using OperatorVector = std::vector<std::unique_ptr<Operator>>;

}  // namespace rhydb::query_engine::filter::operators
