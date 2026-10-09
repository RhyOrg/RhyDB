#include "rhydb/query_engine/scalar_expressions/maybe.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_compilation_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

Maybe::Maybe(std::unique_ptr<ScalarExpression> child)
    : child(std::move(child)) {}

std::string Maybe::toString() const {
   return "Maybe (" + child->toString() + ")";
}

std::vector<schema::ColumnIdentifier> Maybe::freeIUs() const {
   return child->freeIUs();
}

std::unique_ptr<ScalarExpression> Maybe::rewrite(
   const storage::Table& table,
   AmbiguityMode /*mode*/
) const {
   return child->rewrite(table, AmbiguityMode::UPPER_BOUND);
}

std::unique_ptr<filter::operators::Operator> Maybe::compile(
   const storage::Table& /*table*/
) const {
   throw QueryCompilationException{"Maybe expression must be elimitated in query rewrite phase"};
}

}  // namespace rhydb::query_engine::scalar_expressions
