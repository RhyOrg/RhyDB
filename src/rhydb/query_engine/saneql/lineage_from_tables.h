#pragma once

#include <vector>

#include "rhydb/query_engine/saneql/function_registry.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::saneql {

/// The scalar function `lineageFromTables(column, value, lineages, aliases [, includeSublineages]
/// [, recombinantFollowingMode])`: matches `value` and, with `includeSublineages`, its
/// sublineages, over the lineage hierarchy given as the lineage relation table `lineages` and the
/// alias table `aliases`.
ScalarExpressionPtr handleLineageFromTables(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const ScalarConversionContext& context
);

/// The scalar function `lineage(column, value [, includeSublineages] [,
/// recombinantFollowingMode])`: `lineageFromTables` over the lineage relation table `column` and
/// the alias table `column_aliases`, which preprocessing materializes for a column with a lineage
/// definition.
ScalarExpressionPtr handleLineage(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const ScalarConversionContext& context
);

}  // namespace rhydb::query_engine::saneql
