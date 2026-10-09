#pragma once

#include <map>
#include <memory>
#include <string_view>

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::saneql {

operators::QueryNodePtr convertToQueryTree(
   const ast::Expression& ast,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
);

operators::QueryNodePtr parseAndConvertToQueryTree(
   std::string_view query_string,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
);

operators::QueryNodePtr convertExpression(
   const ast::Expression& ast,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
);

/// Converts a saneql expression into a boolean filter predicate. `schema` lists the
/// columns available where the predicate appears (the input table or child node's
/// output), used to resolve referenced columns to their full {name, type}. `tables` gives
/// predicates access to the other tables of the database.
std::unique_ptr<scalar_expressions::ScalarExpression> convertToFilter(
   const ast::Expression& ast,
   const std::vector<schema::ColumnIdentifier>& schema,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
);

/// Like convertToFilter above, but with the full conversion context, so that the predicate may
/// contain subqueries (e.g. `in(<subquery>)`). For scalar function handlers that build filters.
std::unique_ptr<scalar_expressions::ScalarExpression> convertToFilter(
   const ast::Expression& ast,
   const std::vector<schema::ColumnIdentifier>& schema,
   const ScalarConversionContext& context
);

}  // namespace rhydb::query_engine::saneql
