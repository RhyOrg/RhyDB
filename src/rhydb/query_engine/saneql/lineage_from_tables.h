#pragma once

#include <vector>

#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::saneql {

/// The scalar function `lineageFromTables(column, value, lineages, aliases [, includeSublineages]
/// [, recombinantFollowingMode])`: the filter `lineage(column, value, ...)`, over the lineage
/// hierarchy given as the lineage relation table `lineages` and the alias table `aliases` instead
/// of an in-memory lineage index.
ScalarExpressionPtr handleLineageFromTables(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const ScalarConversionContext& context
);

}  // namespace rhydb::query_engine::saneql
