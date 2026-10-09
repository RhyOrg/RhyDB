#include "rhydb/query_engine/scalar_expressions/negation.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <nlohmann/json.hpp>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

Negation::Negation(std::unique_ptr<ScalarExpression> child)
    : child(std::move(child)) {}

std::string Negation::toString() const {
   return "!(" + child->toString() + ")";
}

std::vector<schema::ColumnIdentifier> Negation::freeIUs() const {
   return child->freeIUs();
}

arrow::Result<arrow::compute::Expression> Negation::toArrowExpression() const {
   // Negation is intentionally NOT translated for filters on subexpressions (the Arrow path).
   //
   // In the native bitmap path `!(...)` is a set complement over all rows that includes nulls,
   // which is inconsistent with the equivalent flipped comparison (e.g. `!(age > 50)` != `age <=
   // 50`, they differ by the null rows). Picking a null semantics here would either match the
   // native path (and inherit that inconsistency) or match SQL three-valued logic (and diverge from
   // the native path). Rather than lock in a behaviour that must change once the native
   // inconsistency is resolved, we bail out so subexpression filters containing a negation are
   // rejected. Once #1525 is fixed this should be implemented to match the corrected native
   // semantics.
   return arrow::Status::NotImplemented(
      "negation ('!') is not yet supported in filters on subexpressions (see GitHub issue #1525); "
      "apply the negation in a filter that is pushed into the table scan instead"
   );
}

std::unique_ptr<ScalarExpression> Negation::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<Negation>(child->rewrite(table, invertMode(mode)));
}

std::unique_ptr<filter::operators::Operator> Negation::compile(const storage::Table& table) const {
   return filter::operators::Operator::negate(child->compile(table));
}

}  // namespace rhydb::query_engine::scalar_expressions
