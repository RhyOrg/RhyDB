#pragma once

#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/saneql/function_registry.h"

namespace rhydb::query_engine::saneql {

/// Builds the QueryNode for a `table(...)` literal, e.g. `table({{a:=1, b:=2}, {3, 4}})`. The first
/// row is a record that names the columns; every later row is positional. Each cell must be a
/// constant expression, and all non-null cells of a column must share the same type. The Arrow
/// table is materialized here so cell evaluation errors surface as query errors at plan time.
operators::QueryNodePtr buildTableLiteralNode(const BoundArguments& args, const Tables& tables);

}  // namespace rhydb::query_engine::saneql
