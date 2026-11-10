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
   // Like the native path, this follows three-valued logic: `!null` is null, so a filter drops it
   ARROW_ASSIGN_OR_RAISE(auto child_expression, child->toArrowExpression());
   return arrow::compute::not_(std::move(child_expression));
}

std::unique_ptr<ScalarExpression> Negation::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<Negation>(child->rewrite(table, invertMode(mode)));
}

std::unique_ptr<filter::operators::Operator> Negation::compile(
   const storage::Table& table,
   bool with_null_rows
) const {
   // `!child` is true where `child` is false, i.e. neither true nor null, and null where `child` is
   return filter::operators::Operator::negate(child->compile(table, !with_null_rows));
}

}  // namespace rhydb::query_engine::scalar_expressions
