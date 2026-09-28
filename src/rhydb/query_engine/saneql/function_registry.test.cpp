#include "rhydb/query_engine/saneql/function_registry.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/ast.h"

using rhydb::query_engine::IllegalQueryException;
using rhydb::query_engine::saneql::bindArguments;
using rhydb::query_engine::saneql::BoundArguments;
using rhydb::query_engine::saneql::FunctionSignature;
using rhydb::query_engine::saneql::ParameterDefinition;
using rhydb::query_engine::saneql::ast::makeExpr;
using rhydb::query_engine::saneql::ast::NamedArgument;
using rhydb::query_engine::saneql::ast::PositionalArgument;
using rhydb::query_engine::saneql::ast::StringLiteral;

namespace {

PositionalArgument makePositional(std::string value) {
   return {.value = makeExpr(StringLiteral{std::move(value)}, {}), .location = {}};
}

NamedArgument makeNamed(std::string name, std::string value) {
   return {
      .name = std::move(name),
      .value = makeExpr(StringLiteral{std::move(value)}, {}),
      .location = {},
   };
}

TEST(BindArguments, missingRequiredParameterThrows) {
   FunctionSignature sig{.parameters = {ParameterDefinition{.name = "x", .required = true}}};

   EXPECT_THAT(
      [&sig]() { (void)bindArguments("foo", sig, {}, {}); },
      ThrowsMessage<IllegalQueryException>(::testing::HasSubstr("foo() requires argument 'x'"))
   );
}

TEST(BindArguments, optionalParameterMayBeOmitted) {
   const FunctionSignature sig{.parameters = {ParameterDefinition{.name = "x", .required = false}}};

   ASSERT_NO_THROW(bindArguments("foo", sig, {}, {}));
   const BoundArguments bound = bindArguments("foo", sig, {}, {});
   EXPECT_FALSE(bound.has("x"));
}

TEST(BindArguments, requiredParameterSuppliedPositionally) {
   const FunctionSignature sig{
      .parameters = {ParameterDefinition{.name = "x", .required = true, .positional = true}},
   };

   std::vector<PositionalArgument> pos;
   pos.push_back(makePositional("hello"));
   ASSERT_NO_THROW(bindArguments("foo", sig, pos, {}));
   const BoundArguments bound = bindArguments("foo", sig, pos, {});
   EXPECT_TRUE(bound.has("x"));
}

TEST(BindArguments, requiredParameterSuppliedByName) {
   const FunctionSignature sig{
      .parameters = {ParameterDefinition{.name = "x", .required = true, .positional = false}},
   };

   std::vector<NamedArgument> named;
   named.push_back(makeNamed("x", "hello"));
   ASSERT_NO_THROW(bindArguments("foo", sig, {}, named));
   const BoundArguments bound = bindArguments("foo", sig, {}, named);
   EXPECT_TRUE(bound.has("x"));
}

TEST(BindArguments, tooManyPositionalArgumentsThrows) {
   FunctionSignature sig{.parameters = {ParameterDefinition{.name = "x", .required = true}}};
   std::vector<PositionalArgument> pos;
   pos.push_back(makePositional("value 1"));
   pos.push_back(makePositional("value 2"));
   EXPECT_THAT(
      ([&sig, &pos]() { (void)bindArguments("foo", sig, pos, {}); }),
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("foo() received too many positional arguments")
      )
   );
}

TEST(BindArguments, onlyMissingRequiredParamThrows) {
   FunctionSignature sig{
      .parameters =
         {
            ParameterDefinition{.name = "required_param", .required = true},
            ParameterDefinition{.name = "optional_param", .required = false},
         },
   };
   std::vector<NamedArgument> named;
   named.push_back(makeNamed("optional_param", "val"));
   EXPECT_THAT(
      ([&sig, &named]() { (void)bindArguments("bar", sig, {}, named); }),
      ThrowsMessage<IllegalQueryException>(
         ::testing::HasSubstr("bar() requires argument 'required_param'")
      )
   );
}

}  // namespace
