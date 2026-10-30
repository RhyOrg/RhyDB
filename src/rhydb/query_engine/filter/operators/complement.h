#pragma once

#include <memory>
#include <string>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::filter::operators {

class Complement : public Operator {
   friend class Operator;

   std::unique_ptr<Operator> child;

  public:
   explicit Complement(std::unique_ptr<Operator> child, storage::column::RowLayout row_layout);

   static std::unique_ptr<Complement> fromDeMorgan(
      OperatorVector disjunction,
      storage::column::RowLayout row_layout
   );

   ~Complement() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Complement>&& complement);
};

}  // namespace rhydb::query_engine::filter::operators
