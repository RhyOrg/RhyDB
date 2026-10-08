#include "rhydb/query_engine/operators/compute_filter.h"

#include <memory>
#include <utility>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

using scalar_expressions::ScalarExpression;

CompiledFilter::CompiledFilter(
   std::shared_ptr<const storage::Table> table,
   const ScalarExpression& filter
)
    : table(std::move(table)),
      compiled(
         filter.rewrite(*this->table, ScalarExpression::AmbiguityMode::NONE)->compile(*this->table)
      ) {}

Bitmap CompiledFilter::evaluate() const {
   return compiled->evaluate();
}

std::shared_ptr<const CompiledFilter> compileFilter(
   const std::unique_ptr<ScalarExpression>& filter,
   std::shared_ptr<const storage::Table> table
) {
   return std::make_shared<const CompiledFilter>(std::move(table), *filter);
}

}  // namespace rhydb::query_engine::operators
