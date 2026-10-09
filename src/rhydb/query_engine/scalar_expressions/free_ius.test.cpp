#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "rhydb/query_engine/scalar_expressions/and.h"
#include "rhydb/query_engine/scalar_expressions/int_between.h"
#include "rhydb/query_engine/scalar_expressions/is_null.h"
#include "rhydb/query_engine/scalar_expressions/negation.h"
#include "rhydb/query_engine/scalar_expressions/nof.h"
#include "rhydb/query_engine/scalar_expressions/or.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/query_engine/scalar_expressions/string_in_set.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

namespace {

using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;

const ColumnIdentifier COLUMN_A{.name = "country", .type = ColumnType::DICTIONARY_ENCODED};
const ColumnIdentifier COLUMN_B{.name = "age", .type = ColumnType::INT32};
const ColumnIdentifier COLUMN_C{.name = "date", .type = ColumnType::DATE32};

std::set<ColumnIdentifier> asSet(const std::vector<ColumnIdentifier>& columns) {
   return {columns.begin(), columns.end()};
}

std::unique_ptr<ScalarExpression> stringInSet(const ColumnIdentifier& column) {
   return std::make_unique<StringInSet>(column, std::unordered_set<std::string>{"x"});
}

std::unique_ptr<ScalarExpression> intBetween(const ColumnIdentifier& column) {
   return std::make_unique<IntBetween>(column, std::optional{1}, std::optional{10});
}

std::unique_ptr<ScalarExpression> isNull(const ColumnIdentifier& column) {
   return std::make_unique<IsNull>(column);
}

}  // namespace

TEST(FreeIUs, negationForwardsChild) {
   const Negation negation(isNull(COLUMN_A));
   EXPECT_EQ(asSet(negation.freeIUs()), std::set{COLUMN_A});
}

TEST(FreeIUs, orUnionsChildren) {
   ScalarExpressionVector children;
   children.push_back(stringInSet(COLUMN_A));
   children.push_back(intBetween(COLUMN_B));
   const Or or_expression(std::move(children));
   EXPECT_EQ(asSet(or_expression.freeIUs()), (std::set{COLUMN_A, COLUMN_B}));
}

TEST(FreeIUs, andUnionsChildren) {
   ScalarExpressionVector children;
   children.push_back(stringInSet(COLUMN_A));
   children.push_back(isNull(COLUMN_C));
   const And and_expression(std::move(children));
   EXPECT_EQ(asSet(and_expression.freeIUs()), (std::set{COLUMN_A, COLUMN_C}));
}

TEST(FreeIUs, nofUnionsChildren) {
   ScalarExpressionVector children;
   children.push_back(intBetween(COLUMN_B));
   children.push_back(isNull(COLUMN_C));
   const NOf nof_expression(std::move(children), /*number_of_matchers=*/1, /*match_exactly=*/false);
   EXPECT_EQ(asSet(nof_expression.freeIUs()), (std::set{COLUMN_B, COLUMN_C}));
}

TEST(FreeIUs, nestedBooleanCompositionUnionsAllReferencedColumns) {
   // And([ Or([ A, B ]), NOf([ C ], 1), Negation(A) ])
   ScalarExpressionVector or_children;
   or_children.push_back(stringInSet(COLUMN_A));
   or_children.push_back(intBetween(COLUMN_B));

   ScalarExpressionVector nof_children;
   nof_children.push_back(isNull(COLUMN_C));

   ScalarExpressionVector and_children;
   and_children.push_back(std::make_unique<Or>(std::move(or_children)));
   and_children.push_back(
      std::make_unique<NOf>(
         std::move(nof_children),
         /*number_of_matchers=*/1,
         /*match_exactly=*/false
      )
   );
   and_children.push_back(std::make_unique<Negation>(isNull(COLUMN_A)));

   const And and_expression(std::move(and_children));

   // And/Or/NOf union their children and Negation forwards its child, so every
   // referenced column (A via Or and via Negation, B via Or, C via NOf) appears.
   EXPECT_EQ(asSet(and_expression.freeIUs()), (std::set{COLUMN_A, COLUMN_B, COLUMN_C}));
}

}  // namespace rhydb::query_engine::scalar_expressions
