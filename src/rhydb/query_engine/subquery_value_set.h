#pragma once

#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine {

/// Plans and executes an uncorrelated `subquery` and materializes its single output column into a
/// list of distinct scalar literals, one per non-null value, typed according to the column
/// (string/int/float/date/bool). Used to turn the right-hand side of `column.in(<subquery>)` into a
/// set of equality comparisons — a bitmap union on an indexed column — regardless of the column's
/// type. Throws a query error if the subquery does not produce exactly one supported scalar column.
[[nodiscard]] std::vector<std::unique_ptr<scalar_expressions::ScalarExpression>>
materializeSubqueryColumnLiterals(
   operators::QueryNodePtr subquery,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   std::string_view request_id
);

}  // namespace rhydb::query_engine
