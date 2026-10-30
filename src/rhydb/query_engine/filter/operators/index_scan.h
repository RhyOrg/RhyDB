#pragma once

#include <memory>
#include <optional>
#include <string>

#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/column/row_layout.h"

namespace rhydb::query_engine::scalar_expressions {
// Forward declaration for friend class access. Include would introduce cyclic dependency
class ScalarExpression;
}  // namespace rhydb::query_engine::scalar_expressions

namespace rhydb::query_engine::filter::operators {

class IndexScan : public Operator {
   friend class Operator;

  private:
   std::optional<std::unique_ptr<scalar_expressions::ScalarExpression>> logical_equivalent;
   Bitmap bitmap;

  public:
   explicit IndexScan(Bitmap bitmap, storage::column::RowLayout row_layout);

   explicit IndexScan(
      std::unique_ptr<query_engine::scalar_expressions::ScalarExpression>&& logical_equivalent,
      Bitmap bitmap,
      storage::column::RowLayout row_layout
   );

   ~IndexScan() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] roaring_util::CopyOnWriteContainer evaluateChunk(uint16_t chunk_id) const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<IndexScan>&& index_scan);
};

}  // namespace rhydb::query_engine::filter::operators
