#include "rhydb/query_engine/scalar_expressions/exact.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

Exact::Exact(std::unique_ptr<ScalarExpression> child)
    : child(std::move(child)) {}

std::string Exact::toString() const {
   return fmt::format("Exact ({})", child->toString());
}

std::vector<schema::ColumnIdentifier> Exact::freeIUs() const {
   return child->freeIUs();
}

std::unique_ptr<ScalarExpression> Exact::rewrite(
   const storage::Table& table,
   AmbiguityMode /*mode*/
) const {
   return child->rewrite(table, AmbiguityMode::LOWER_BOUND);
}

std::unique_ptr<filter::operators::Operator> Exact::compile(
   const storage::Table& /*table*/
) const {
   throw QueryCompilationException{"Exact expression must be elimitated in query rewrite phase"};
}

}  // namespace rhydb::query_engine::scalar_expressions
