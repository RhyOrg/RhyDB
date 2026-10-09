#include "rhydb/query_engine/saneql/lineage_from_tables.h"

#include <string>

#include <fmt/format.h>

#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/query_engine/saneql/parser.h"

namespace rhydb::query_engine::saneql {

namespace {

/// The edges of the lineage hierarchy, as a table with columns `edge_from` and `edge_to`. Without
/// sublineages, these are just the edges between each lineage and its aliases, in both directions:
/// walking them from a lineage or alias reaches its canonical lineage and all aliases thereof. With
/// sublineages, they also contain the edges from each lineage to its children: 'doNotFollow' drops
/// the edges into recombinant lineages, 'alwaysFollow' keeps them, and
/// 'followIfFullyContainedInClade' attaches each recombinant lineage to its clade ancestor instead
/// of to its parents. Since every edge but the alias edges points downwards, walking the edges
/// never leaves the lineages below the canonical lineage of the start.
std::string lineageEdgesQuery(
   const std::string& lineages,
   const std::string& aliases,
   bool include_sublineages,
   const std::string& recombinant_following_mode
) {
   std::string alias_edges = fmt::format(
      "{aliases}.map({{edge_from := lineage, edge_to := alias}}).project({{edge_from, edge_to}})"
      ".unionall({aliases}.map({{edge_from := alias, edge_to := lineage}})"
      ".project({{edge_from, edge_to}}))",
      fmt::arg("aliases", aliases)
   );
   if (!include_sublineages) {
      return alias_edges;
   }
   const std::string parent_edges = fmt::format(
      "{}.filter({}).map({{edge_from := parent, edge_to := lineage}})"
      ".project({{edge_from, edge_to}})",
      lineages,
      recombinant_following_mode == "alwaysFollow" ? "true" : "is_recombinant_edge = false"
   );
   if (recombinant_following_mode != "followIfFullyContainedInClade") {
      return fmt::format("{}.unionall({})", parent_edges, alias_edges);
   }
   return fmt::format(
      "{}.unionall({}.filter(is_recombinant_edge = true)"
      ".map({{edge_from := recombinant_clade_ancestor, edge_to := lineage}})"
      ".project({{edge_from, edge_to}})).unionall({})",
      parent_edges,
      lineages,
      alias_edges
   );
}

}  // namespace

/// Matches the canonical lineage of `value` and its aliases, and with `includeSublineages` all
/// lineages below it and their aliases, as all lineages reachable from `value` by
/// lineageEdgesQuery:
///
///    column = value
///    || column.in(<edges>.transitiveClosure(edge_from, edge_to, startingFrom := {value})
///                    .project({to}))
///
/// (`column = value` matches `value` even if it has no edges at all.)
///
/// This is a pure rewrite into other functions of the query language, which compute the result
/// from the given tables when the query runs.
ScalarExpressionPtr handleLineageFromTables(
   const BoundArguments& args,
   const std::vector<schema::ColumnIdentifier>& schema,
   const ScalarConversionContext& context
) {
   const std::string column = ast::quoteIdentifier(ast::extractIdentifierName(args.at("column")));
   const auto& value_expression = args.at("value");
   CHECK_RHYDB_QUERY(
      !ast::isNullLiteral(value_expression),
      "lineageFromTables() does not accept null as the lineage; use isNull({}) instead",
      column
   );
   const std::string value = value_expression.toString();
   const std::string lineages =
      ast::quoteIdentifier(ast::extractIdentifierName(args.at("lineages")));
   const std::string aliases = ast::quoteIdentifier(ast::extractIdentifierName(args.at("aliases")));
   bool include_sublineages = false;
   if (const auto* expression = args.get("includeSublineages")) {
      include_sublineages = ast::extractBoolLiteral(*expression);
   }
   const std::string recombinant_following_mode =
      args.getOptionalString("recombinantFollowingMode").value_or("doNotFollow");
   CHECK_RHYDB_QUERY(
      recombinant_following_mode == "doNotFollow" || recombinant_following_mode == "alwaysFollow" ||
         recombinant_following_mode == "followIfFullyContainedInClade",
      "invalid recombinantFollowingMode: '{}'. Valid values are: alwaysFollow, "
      "followIfFullyContainedInClade, doNotFollow",
      recombinant_following_mode
   );

   const std::string query = fmt::format(
      "{column} = {value} || {column}.in({edges}"
      ".transitiveClosure(edge_from, edge_to, startingFrom := {{{value}}})"
      ".project({{to}}))",
      fmt::arg("column", column),
      fmt::arg("value", value),
      fmt::arg(
         "edges",
         lineageEdgesQuery(lineages, aliases, include_sublineages, recombinant_following_mode)
      )
   );
   return convertToFilter(*Parser(query).parse(), schema, context);
}

}  // namespace rhydb::query_engine::saneql
