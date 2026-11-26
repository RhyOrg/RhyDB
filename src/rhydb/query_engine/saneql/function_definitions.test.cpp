#include "rhydb/query_engine/saneql/function_definitions.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/parse_exception.h"
#include "rhydb/query_engine/saneql/parser.h"

using rhydb::query_engine::IllegalQueryException;
using rhydb::query_engine::saneql::expandFunctionDefinitions;
using rhydb::query_engine::saneql::FunctionDefinitions;
using rhydb::query_engine::saneql::ParameterType;
using rhydb::query_engine::saneql::ParseException;
using rhydb::query_engine::saneql::parseQuery;
using rhydb::query_engine::saneql::Parser;
namespace ast = rhydb::query_engine::saneql::ast;
using ::testing::HasSubstr;

namespace {

std::string expanded(std::string_view query) {
   return parseQuery(query)->toString();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FunctionDefinitions, parsesDefinitions) {
   auto program =
      Parser("let f(a expression, b expression := 1) := a < b, let g := true, data").parseProgram();
   ASSERT_EQ(program.definitions.size(), 2);
   const auto& definition = program.definitions.front();
   EXPECT_EQ(definition.name, "f");
   ASSERT_EQ(definition.parameters.size(), 2);
   EXPECT_EQ(definition.parameters[0].name, "a");
   EXPECT_EQ(definition.parameters[0].type, ParameterType::EXPRESSION);
   EXPECT_EQ(definition.parameters[0].default_value, nullptr);
   EXPECT_EQ(definition.parameters[1].name, "b");
   EXPECT_EQ(definition.parameters[1].default_value->toString(), "1");
   EXPECT_EQ(definition.body->toString(), "(a < b)");
   EXPECT_EQ(program.definitions[1].name, "g");
   EXPECT_TRUE(program.definitions[1].parameters.empty());
   EXPECT_EQ(program.body->toString(), "data");
}

TEST(FunctionDefinitions, parsesScalarParameters) {
   auto program = Parser("let f(a scalar, b scalar := 1) := a < b, data").parseProgram();
   const auto& parameters = program.definitions.front().parameters;
   ASSERT_EQ(parameters.size(), 2);
   EXPECT_EQ(parameters[0].type, ParameterType::SCALAR);
   EXPECT_EQ(parameters[1].type, ParameterType::SCALAR);
}

TEST(FunctionDefinitions, rejectsParametersWithoutTypes) {
   EXPECT_THAT(
      [] { (void)Parser("let f(a, b scalar := 1) := a < b, data").parseProgram(); },
      ThrowsMessage<ParseException>(HasSubstr(
         "Parse error at 1:8: Expected the type of parameter 'a': 'scalar', 'table', "
         "'expression' or 'symbol'"
      ))
   );
   EXPECT_THAT(
      [] { (void)Parser("let f(a := 1) := a, data").parseProgram(); },
      ThrowsMessage<ParseException>(HasSubstr("Expected the type of parameter 'a'"))
   );
}

TEST(FunctionDefinitions, definitionRequiresComma) {
   EXPECT_THAT(
      [] { (void)Parser("let f(a expression) := a data").parseProgram(); },
      ThrowsMessage<ParseException>(HasSubstr("Expected Comma"))
   );
}

TEST(FunctionDefinitions, emptyParameterListIsRejected) {
   EXPECT_THAT(
      [] { (void)Parser("let f() := true, data").parseProgram(); },
      ThrowsMessage<ParseException>(HasSubstr("Expected Identifier"))
   );
}

TEST(FunctionDefinitions, queryWithoutDefinitionsIsUnchanged) {
   EXPECT_EQ(expanded("data.filter(x = 1)"), "filter(data, (x = 1))");
}

TEST(FunctionDefinitions, substitutesArgumentsForParameters) {
   EXPECT_EQ(
      expanded(
         "let between3(c expression, lo expression) := c >= lo && c <= 3, "
         "data.filter(age.between3(1))"
      ),
      "filter(data, ((age >= 1) && (age <= 3)))"
   );
}

TEST(FunctionDefinitions, substitutesDefaultsAndNamedArguments) {
   const std::string definition =
      "let atMost(c expression, limit expression := 10) := c <= limit, ";
   EXPECT_EQ(expanded(definition + "data.filter(atMost(age))"), "filter(data, (age <= 10))");
   EXPECT_EQ(
      expanded(definition + "data.filter(atMost(age, limit := 5))"), "filter(data, (age <= 5))"
   );
}

TEST(FunctionDefinitions, definitionWithoutParametersIsReferencedByName) {
   EXPECT_EQ(
      expanded("let base := '1996-01-01'::date, data.filter(date >= base)"),
      "filter(data, (date >= '1996-01-01'::date))"
   );
   EXPECT_EQ(
      expanded("let selected := data.filter(x = 1), selected.limit(10)"),
      "limit(filter(data, (x = 1)), 10)"
   );
}

TEST(FunctionDefinitions, argumentsMayBeTables) {
   EXPECT_EQ(
      expanded("let firstTen(t expression) := t.limit(10), firstTen(data.filter(x = 1))"),
      "limit(filter(data, (x = 1)), 10)"
   );
}

TEST(FunctionDefinitions, bodyKeepsIdentifiersThatAreNoParameters) {
   EXPECT_EQ(
      expanded("let isTarget(value expression) := lineage = value, data.filter(isTarget('A'))"),
      "filter(data, (lineage = 'A'))"
   );
}

TEST(FunctionDefinitions, definitionsMayCallEarlierDefinitions) {
   EXPECT_EQ(
      expanded(
         "let inner(x expression) := x = 1, "
         "let outer(y expression) := inner(y) || inner(z), "
         "data.filter(outer(a))"
      ),
      "filter(data, ((a = 1) || (z = 1)))"
   );
}

TEST(FunctionDefinitions, definitionsDoNotSeeLaterDefinitions) {
   EXPECT_EQ(
      expanded(
         "let outer(y expression) := inner(y), let inner(x expression) := x = 1, "
         "data.filter(outer(a))"
      ),
      "filter(data, inner(a))"
   );
}

TEST(FunctionDefinitions, definitionsCannotRecurse) {
   EXPECT_EQ(expanded("let f(a expression) := f(a), data.filter(f(1))"), "filter(data, f(1))");
}

TEST(FunctionDefinitions, argumentsAreExpandedWhereTheCallIs) {
   FunctionDefinitions outer;
   outer.addFromSource(
      "let helper(x expression) := x = 1, let wrap(x expression) := helper(x) || helper(b),"
   );
   FunctionDefinitions inner{&outer};
   inner.addFromSource("let helper(x expression) := x = 2,");
   // `helper` in the argument is the caller's, `helper` in the body of `wrap` is `wrap`'s own.
   const auto expression = Parser("wrap(helper(a))").parse();
   EXPECT_EQ(
      expandFunctionDefinitions(*expression, inner)->toString(), "(((a = 2) = 1) || (b = 1))"
   );
}

TEST(FunctionDefinitions, innerDefinitionsShadowOuterDefinitions) {
   FunctionDefinitions outer;
   outer.addFromSource("let f(x expression) := x = 1,");
   FunctionDefinitions inner{&outer};
   inner.addFromSource("let f(x expression) := x = 2,");
   const auto expression = Parser("f(a)").parse();
   EXPECT_EQ(expandFunctionDefinitions(*expression, inner)->toString(), "(a = 2)");
   EXPECT_EQ(expandFunctionDefinitions(*expression, outer)->toString(), "(a = 1)");
}

TEST(FunctionDefinitions, definitionsShadowBuiltinFunctions) {
   EXPECT_EQ(
      expanded("let limit(t expression, n expression) := t.offset(n), data.limit(3)"),
      "offset(data, 3)"
   );
}

TEST(FunctionDefinitions, rejectsWrongArguments) {
   EXPECT_THAT(
      [] { (void)parseQuery("let f(a expression) := a, data.filter(f())"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("f() requires argument 'a'"))
   );
   EXPECT_THAT(
      [] { (void)parseQuery("let f(a expression) := a, data.filter(f(1, b := 2))"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("'b'"))
   );
}

TEST(FunctionDefinitions, rejectsDuplicateDefinitions) {
   EXPECT_THAT(
      [] { (void)parseQuery("let f := true, let f := false, data"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("duplicate let 'f' at 1:16"))
   );
}

TEST(FunctionDefinitions, rejectsDuplicateParameters) {
   EXPECT_THAT(
      [] { (void)parseQuery("let f(a expression, a expression) := a, data"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("duplicate parameter 'a' of let 'f'"))
   );
}

TEST(FunctionDefinitions, rejectsUnknownParameterTypes) {
   EXPECT_THAT(
      [] { (void)parseQuery("let f(a relation) := a, data"); },
      ThrowsMessage<ParseException>(
         HasSubstr("Parse error at 1:9: Unknown type 'relation' of parameter 'a'")
      )
   );
}

TEST(FunctionDefinitions, tableParameterTakesTables) {
   const std::string definition = "let firstTen(t table) := t.limit(10), ";
   EXPECT_EQ(expanded(definition + "firstTen(data)"), "limit(data, 10)");
   EXPECT_EQ(
      expanded(definition + "firstTen(data.filter(x = 1))"), "limit(filter(data, (x = 1)), 10)"
   );
   EXPECT_THAT(
      [&] { (void)parseQuery(definition + "firstTen(1)"); },
      ThrowsMessage<IllegalQueryException>(
         HasSubstr("firstTen(): the argument 't' must be a table, but is 1 at")
      )
   );
}

TEST(FunctionDefinitions, viewsAreDefinitionsWithoutParameters) {
   EXPECT_EQ(
      expanded(
         "let recent := data.filter(date >= '2021-01-01'::date).project({country, date}),\n"
         "let germanRecent := recent.filter(country = 'Germany'),\n"
         "germanRecent.limit(5)"
      ),
      "limit(filter(project(filter(data, (date >= '2021-01-01'::date)), {country, date}), "
      "(country = 'Germany')), 5)"
   );
}

TEST(FunctionDefinitions, scalarParameterTakesConstants) {
   const std::string definition = "let older(t table, age scalar := 18) := t.filter(years > age), ";
   EXPECT_EQ(expanded(definition + "older(data)"), "filter(data, (years > 18))");
   EXPECT_EQ(expanded(definition + "older(data, 65)"), "filter(data, (years > 65))");
   EXPECT_THAT(
      [&] { (void)parseQuery(definition + "older(data, other_column)"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr(
         "older(): the argument 'age' must be a constant, as it is a scalar, but other_column at "
      ))
   );
}

TEST(FunctionDefinitions, defaultValuesMustFitTheirParameter) {
   EXPECT_THAT(
      [] { (void)parseQuery("let f(age scalar := other_column) := age, data"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("the argument 'age' must be a constant"))
   );
}

TEST(FunctionDefinitions, symbolParameterRenamesColumnsAndFields) {
   const std::string definition =
      "let withDouble(t table, source symbol, target symbol) := "
      "t.map({target := source}).project({source, target}), ";
   EXPECT_EQ(
      expanded(definition + "withDouble(data, age, age2)"),
      "project(map(data, {age2:=age}), {age, age2})"
   );
   EXPECT_THAT(
      [&] { (void)parseQuery(definition + "withDouble(data, 'age', age2)"); },
      ThrowsMessage<IllegalQueryException>(
         HasSubstr("withDouble(): the argument 'source' must be a name, but is 'age' at 1:")
      )
   );
}

TEST(FunctionDefinitions, argumentsAreNotCapturedByInnerDefinitions) {
   // `a` in the argument of `outer` is the caller's column, not the parameter `a` of `inner`.
   EXPECT_EQ(
      expanded(
         "let inner(a expression, b expression) := a = b, "
         "let outer(b expression) := inner(1, b), "
         "data.filter(outer(a))"
      ),
      "filter(data, (1 = a))"
   );
}

TEST(FunctionDefinitions, limitsTheNestingOfCalls) {
   FunctionDefinitions definitions;
   definitions.addFromSource(
      "let f1(x expression) := x, let f2(x expression) := f1(x), let f3(x expression) := f2(x),"
   );
   const auto expression = Parser("f3(1)").parse();
   EXPECT_EQ(
      expandFunctionDefinitions(*expression, definitions, {.max_nested_calls = 3})->toString(), "1"
   );
   EXPECT_THAT(
      [&] { (void)expandFunctionDefinitions(*expression, definitions, {.max_nested_calls = 2}); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("nests more than 2 calls of definitions"))
   );
}

TEST(FunctionDefinitions, limitsTheSizeOfTheExpansion) {
   // Every definition doubles the size of the expansion.
   std::string query = "let f0(x expression) := x && x,";
   for (int i = 1; i <= 30; ++i) {
      query += fmt::format(" let f{}(x expression) := f{}(f{}(x)),", i, i - 1, i - 1);
   }
   query += " data.filter(f30(a))";
   EXPECT_THAT(
      [&] { (void)parseQuery(query); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("creates more than 1000000 nodes"))
   );
}

TEST(FunctionDefinitions, limitsTheDepthOfTheExpansion) {
   FunctionDefinitions definitions;
   definitions.addFromSource("let deep(x expression) := x.f().f().f().f().f(),");
   const auto expression = Parser("deep(deep(a))").parse();
   EXPECT_NO_THROW((void)expandFunctionDefinitions(*expression, definitions, {.max_depth = 11}));
   EXPECT_THAT(
      [&] { (void)expandFunctionDefinitions(*expression, definitions, {.max_depth = 10}); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("the maximum nesting depth is 10"))
   );
}

TEST(FunctionDefinitions, expansionTakesTheLocationOfTheCall) {
   const auto expression = parseQuery("let f(a expression) := a = unknown,\ndata.filter(f(1))");
   const auto& filter = std::get<rhydb::query_engine::saneql::ast::FunctionCall>(expression->value);
   const auto& condition = *filter.positional_arguments.at(1).value;
   EXPECT_EQ(condition.location.line, 2);
   EXPECT_EQ(condition.location.column, 13);
}

// The example of query_documentation.md.
TEST(FunctionDefinitions, documentationExample) {
   EXPECT_EQ(
      expanded(
         "let since := '2021-01-01'::date,\n"
         "let recent(t expression) := t.filter(date >= since),\n"
         "let inCountries(c expression) := c.in({'Germany', 'Switzerland'}),\n"
         "data.recent().filter(country.inCountries())"
      ),
      "filter(filter(data, (date >= '2021-01-01'::date)), in(country, {'Germany', "
      "'Switzerland'}))"
   );
}

}  // namespace
