#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rhydb/common/bitmap.h"

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

class Operator {
  public:
   Operator();

   virtual ~Operator() noexcept;

   [[nodiscard]] virtual Type type() const = 0;

   [[nodiscard]] virtual Bitmap evaluate() const = 0;

   [[nodiscard]] virtual std::string toString() const = 0;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Operator>&& some_operator);
};

using OperatorVector = std::vector<std::unique_ptr<Operator>>;

}  // namespace rhydb::query_engine::filter::operators
