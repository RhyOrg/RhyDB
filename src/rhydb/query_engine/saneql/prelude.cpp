#include "rhydb/query_engine/saneql/prelude.h"

#include <string>
#include <string_view>

#include "rhydb/common/lineage_tree.h"
#include "rhydb/initialize/lineage_relation_table.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"

namespace rhydb::query_engine::saneql {

namespace {

/// The native definitions: rewrites that cannot be written in SaneQL.
FunctionDefinitions makeNativeDefinitions() {
   FunctionDefinitions definitions;
   // `lineageAliasTable(column)`: the name of the alias table that preprocessing materializes for
   // the lineage column `column`.
   definitions.addNative(
      "lineageAliasTable",
      {{.name = "column", .type = ParameterType::SYMBOL}},
      [](const BoundArguments& arguments, const SourceLocation& call_location) {
         return ast::makeExpr(
            ast::Identifier{
               initialize::lineageAliasTableName(
                  ast::extractIdentifierName(arguments.at("column"))
               ),
            },
            call_location
         );
      }
   );
   // `isRecombinantFollowingMode(mode, name)`: whether the recombinantFollowingMode `mode` is the
   // one named `name`, as a bool literal. Throws if `mode` names no mode.
   definitions.addNative(
      "isRecombinantFollowingMode",
      {
         {.name = "mode", .type = ParameterType::SCALAR},
         {.name = "name", .type = ParameterType::SCALAR},
      },
      [](const BoundArguments& arguments, const SourceLocation& call_location) {
         const std::string mode = ast::extractStringLiteral(arguments.at("mode"));
         CHECK_RHYDB_QUERY(
            common::recombinantEdgeFollowingModeFromName(mode).has_value(),
            "invalid recombinantFollowingMode: '{}'. Valid values are: {}, {}, {}",
            mode,
            common::ALWAYS_FOLLOW_NAME,
            common::FOLLOW_IF_FULLY_CONTAINED_IN_CLADE_NAME,
            common::DO_NOT_FOLLOW_NAME
         );
         return ast::makeExpr(
            ast::BoolLiteral{.value = mode == ast::extractStringLiteral(arguments.at("name"))},
            call_location
         );
      }
   );
   return definitions;
}

const std::string_view PRELUDE = R"(
-- The edges of the lineage hierarchy of the lineage relation table `lineages` and the alias table
-- `aliases`, as a table with columns `edge_from` and `edge_to`: a self-edge for each lineage and
-- the edges between each lineage and its aliases, in both directions, so that walking them from a
-- lineage or alias reaches its canonical lineage and all aliases thereof. The self-edges make
-- every lineage reach itself, even one without any other edge, while a value that is no lineage or
-- alias reaches nothing. With `includeSublineages`, also the edges from each lineage to its
-- children: 'doNotFollow' drops the edges into recombinant lineages, 'alwaysFollow' keeps them, and
-- 'followIfFullyContainedInClade' attaches each recombinant lineage to its clade ancestor instead
-- of to its parents. Since all of these edges but the alias edges point downwards, walking them
-- never leaves the lineages below the canonical lineage of the start.
let lineageEdges(
   lineages table,
   aliases table,
   includeSublineages scalar := false,
   recombinantFollowingMode scalar := 'doNotFollow'
) :=
   lineages
      .filter(includeSublineages)
      .filter(is_recombinant_edge = false
         || isRecombinantFollowingMode(recombinantFollowingMode, 'alwaysFollow'))
      .map({edge_from := parent, edge_to := lineage})
      .project({edge_from, edge_to})
   .unionall(lineages
      .filter(includeSublineages)
      .filter(isRecombinantFollowingMode(recombinantFollowingMode, 'followIfFullyContainedInClade'))
      .filter(is_recombinant_edge = true)
      .map({edge_from := recombinant_clade_ancestor, edge_to := lineage})
      .project({edge_from, edge_to}))
   .unionall(lineages
      .map({edge_from := lineage, edge_to := lineage})
      .project({edge_from, edge_to}))
   .unionall(aliases
      .map({edge_from := lineage, edge_to := alias})
      .project({edge_from, edge_to}))
   .unionall(aliases
      .map({edge_from := alias, edge_to := lineage})
      .project({edge_from, edge_to})),

-- Matches the canonical lineage of `value` and its aliases, and with `includeSublineages` all
-- lineages below it and their aliases, over the lineage hierarchy given as the lineage relation
-- table `lineages` and the alias table `aliases`.
let lineageFromTables(
   column expression,
   value expression,
   lineages table,
   aliases table,
   includeSublineages scalar := false,
   recombinantFollowingMode scalar := 'doNotFollow'
) :=
   column.in(
      lineageEdges(lineages, aliases, includeSublineages, recombinantFollowingMode)
         .transitiveClosure(edge_from, edge_to, startingFrom := {value})
         .project({to})
   ),

-- lineageFromTables over the lineage relation table `column` and the alias table that
-- preprocessing materializes for a column with a lineage definition.
let lineage(
   column symbol,
   value expression,
   includeSublineages scalar := false,
   recombinantFollowingMode scalar := 'doNotFollow'
) :=
   lineageFromTables(
      column, value, column, lineageAliasTable(column), includeSublineages, recombinantFollowingMode
   ),
)";

}  // namespace

const FunctionDefinitions& builtinDefinitions() {
   static const FunctionDefinitions native_definitions = makeNativeDefinitions();
   static const FunctionDefinitions prelude = [] {
      FunctionDefinitions result{&native_definitions};
      result.addFromSource(PRELUDE);
      return result;
   }();
   return prelude;
}

}  // namespace rhydb::query_engine::saneql
