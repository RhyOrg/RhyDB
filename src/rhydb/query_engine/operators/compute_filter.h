#pragma once

#include <memory>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

class CompiledFilter {
   std::shared_ptr<const storage::Table> table;
   std::unique_ptr<filter::operators::Operator> compiled;

  public:
   CompiledFilter(
      std::shared_ptr<const storage::Table> table,
      const scalar_expressions::ScalarExpression& filter
   );

   [[nodiscard]] Bitmap evaluate() const;
};

std::shared_ptr<const CompiledFilter> compileFilter(
   const std::unique_ptr<scalar_expressions::ScalarExpression>& filter,
   std::shared_ptr<const storage::Table> table
);

}  // namespace rhydb::query_engine::operators
