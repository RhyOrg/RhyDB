#pragma once

#include <memory>
#include <string>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::scalar_expressions {
// Forward declaration for friend class access. Include would introduce cyclic dependency
class And;
class NOf;
}  // namespace rhydb::query_engine::scalar_expressions

namespace rhydb::query_engine::filter::operators {

class Intersection : public Operator {
   friend class rhydb::query_engine::scalar_expressions::And;
   friend class rhydb::query_engine::scalar_expressions::NOf;

   OperatorVector children;
   OperatorVector negated_children;

  public:
   explicit Intersection(
      OperatorVector&& children,
      OperatorVector&& negated_children,
      storage::column::RowLayout row_layout
   );

   /// Builds `!child1 && !child2 && ...`, the intersection of the complements of `children`
   /// - equivalently every row that none of the children select.
   static std::unique_ptr<Operator> ofComplements(
      OperatorVector children,
      storage::column::RowLayout row_layout
   );

   ~Intersection() noexcept override;

   [[nodiscard]] std::string toString() const override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Intersection>&& intersection);
};

}  // namespace rhydb::query_engine::filter::operators
