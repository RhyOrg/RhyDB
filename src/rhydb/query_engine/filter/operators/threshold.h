#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::filter::operators {

class Threshold : public Operator {
  private:
   OperatorVector non_negated_children;
   OperatorVector negated_children;
   uint32_t number_of_matchers;
   bool match_exactly;

  public:
   Threshold(
      OperatorVector&& non_negated_children,
      OperatorVector&& negated_children,
      uint32_t number_of_matchers,
      bool match_exactly,
      storage::column::RowLayout row_layout
   );

   ~Threshold() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Threshold>&& threshold);
};

}  // namespace rhydb::query_engine::filter::operators
