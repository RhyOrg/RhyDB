#include "rhydb/query_engine/scalar_expressions/concat.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <arrow/scalar.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::scalar_expressions {

Concat::Concat(std::unique_ptr<ScalarExpression> left, std::unique_ptr<ScalarExpression> right)
    : left(std::move(left)),
      right(std::move(right)) {
   RHYDB_ASSERT(this->left != nullptr);
   RHYDB_ASSERT(this->right != nullptr);
}

std::string Concat::toString() const {
   return fmt::format("concat({}, {})", left->toString(), right->toString());
}

std::vector<schema::ColumnIdentifier> Concat::freeIUs() const {
   std::vector<schema::ColumnIdentifier> result = left->freeIUs();
   std::ranges::move(right->freeIUs(), std::back_inserter(result));
   return result;
}

arrow::Result<arrow::compute::Expression> Concat::toArrowExpression() const {
   ARROW_ASSIGN_OR_RAISE(auto left_expression, left->toArrowExpression());
   ARROW_ASSIGN_OR_RAISE(auto right_expression, right->toArrowExpression());

   return arrow::compute::call(
      "binary_join_element_wise",
      {
         std::move(left_expression),
         std::move(right_expression),
         arrow::compute::literal(std::make_shared<arrow::LargeStringScalar>("")),
      }
   );
}

std::unique_ptr<ScalarExpression> Concat::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<Concat>(left->rewrite(table, mode), right->rewrite(table, mode));
}

std::unique_ptr<filter::operators::Operator> Concat::compile(
   const storage::Table& /*table*/
) const {
   RHYDB_UNIMPLEMENTED();
}

}  // namespace rhydb::query_engine::scalar_expressions
