// Filters follow three-valued (Kleene) logic: a row evaluates to true, false or null. A filter
// selects its true rows, its negation selects its false rows. These tests check filters against a
// brute-force evaluation on all combinations of the truth values of three inputs, one combination
// per row. Each input is available as several columns, so that the filters cover the different
// ways comparisons are compiled (index scans, predicates, dictionary lookups, ...).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {

using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

enum class Truth : uint8_t { TRUE_VALUE, FALSE_VALUE, NULL_VALUE };

constexpr uint32_t NUM_INPUTS = 3;
// Every combination of the truth values of the inputs
constexpr uint32_t NUM_ROWS = 27;

/// The truth value of input `input` on row `row`: the row's base-3 digit `input`
Truth inputValue(uint32_t input, uint32_t row) {
   for (uint32_t digit = 0; digit < input; ++digit) {
      row /= 3;
   }
   return static_cast<Truth>(row % 3);
}

Truth kleeneNot(Truth value) {
   switch (value) {
      case Truth::TRUE_VALUE:
         return Truth::FALSE_VALUE;
      case Truth::FALSE_VALUE:
         return Truth::TRUE_VALUE;
      case Truth::NULL_VALUE:
         return Truth::NULL_VALUE;
   }
   return value;
}

/// Evaluates `nOf(number_of_matchers, values, matchExactly)`: the null values might be true or
/// false, so the number of true values lies in [lowest, highest]
Truth nOf(const std::vector<Truth>& values, uint32_t number_of_matchers, bool match_exactly) {
   const auto lowest = static_cast<uint32_t>(std::ranges::count(values, Truth::TRUE_VALUE));
   const auto highest =
      lowest + static_cast<uint32_t>(std::ranges::count(values, Truth::NULL_VALUE));
   if (match_exactly) {
      if (lowest == number_of_matchers && highest == number_of_matchers) {
         return Truth::TRUE_VALUE;
      }
      if (number_of_matchers < lowest || highest < number_of_matchers) {
         return Truth::FALSE_VALUE;
      }
      return Truth::NULL_VALUE;
   }
   if (lowest >= number_of_matchers) {
      return Truth::TRUE_VALUE;
   }
   if (highest < number_of_matchers) {
      return Truth::FALSE_VALUE;
   }
   return Truth::NULL_VALUE;
}

Truth conjunction(const std::vector<Truth>& values) {
   return nOf(values, static_cast<uint32_t>(values.size()), false);
}

Truth disjunction(const std::vector<Truth>& values) {
   return nOf(values, 1, false);
}

nlohmann::json valueOf(Truth truth, nlohmann::json if_true, nlohmann::json if_false) {
   switch (truth) {
      case Truth::TRUE_VALUE:
         return if_true;
      case Truth::FALSE_VALUE:
         return if_false;
      case Truth::NULL_VALUE:
         return nullptr;
   }
   return nullptr;
}

std::string primaryKey(uint32_t row) {
   return fmt::format("row_{:02}", row);
}

std::vector<nlohmann::json> truthTable() {
   std::vector<nlohmann::json> rows;
   for (uint32_t row = 0; row < NUM_ROWS; ++row) {
      nlohmann::json record{{"primaryKey", primaryKey(row)}};
      for (uint32_t input = 0; input < NUM_INPUTS; ++input) {
         const Truth truth = inputValue(input, row);
         record[fmt::format("b{}", input)] = valueOf(truth, true, false);
         record[fmt::format("n{}", input)] = valueOf(truth, 1, 0);
         record[fmt::format("f{}", input)] = valueOf(truth, 1.0, 0.0);
         record[fmt::format("d{}", input)] = valueOf(truth, "yes", "no");
         record[fmt::format("s{}", input)] = valueOf(truth, "yes", "no");
      }
      rows.push_back(record);
   }
   return rows;
}

std::string databaseConfig() {
   std::string metadata = "    - name: \"primaryKey\"\n      type: \"string\"\n";
   for (uint32_t input = 0; input < NUM_INPUTS; ++input) {
      metadata += fmt::format(
         "    - name: \"b{0}\"\n      type: \"boolean\"\n"
         "    - name: \"n{0}\"\n      type: \"int\"\n"
         "    - name: \"f{0}\"\n      type: \"float\"\n"
         "    - name: \"d{0}\"\n      type: \"string\"\n      generateIndex: true\n"
         "    - name: \"s{0}\"\n      type: \"string\"\n",
         input
      );
   }
   return "schema:\n  instanceName: \"three valued logic\"\n  metadata:\n" + metadata +
          "  primaryKey: \"primaryKey\"\n";
}

const QueryTestData TEST_DATA{
   .ndjson_input_data = truthTable(),
   .database_config = databaseConfig(),
   .reference_genomes = ReferenceGenomes{{}, {}},
};

/// The filters on input `input` that are true, false and null where the input is
const std::vector<std::string> ATOMS{
   "b{} = true",
   "n{} = 1",
   "f{} >= 0.5",
   "d{} = 'yes'",
   "d{} <> 'no'",
   "s{} = 'yes'",
   "b{} <> false",
   "n{} > 0",
};

struct Template {
   std::string name;
   /// Builds the filter from the filters on the three inputs
   std::function<std::string(const std::vector<std::string>&)> filter;
   std::function<Truth(const std::vector<Truth>&)> expected;
};

const std::vector<Template> TEMPLATES{
   {
      .name = "single",
      .filter = [](const auto& arg) { return arg[0]; },
      .expected = [](const auto& val) { return val[0]; },
   },
   {
      .name = "doubleNegation",
      .filter = [](const auto& arg) { return fmt::format("!(!({}))", arg[0]); },
      .expected = [](const auto& val) { return val[0]; },
   },
   {
      .name = "and",
      .filter =
         [](const auto& arg) { return fmt::format("{} && {} && {}", arg[0], arg[1], arg[2]); },
      .expected = [](const auto& val) { return conjunction(val); },
   },
   {
      .name = "or",
      .filter =
         [](const auto& arg) { return fmt::format("{} || {} || {}", arg[0], arg[1], arg[2]); },
      .expected = [](const auto& val) { return disjunction(val); },
   },
   {
      .name = "andWithNegation",
      .filter = [](const auto& arg) { return fmt::format("{} && !({})", arg[0], arg[1]); },
      .expected = [](const auto& val) { return conjunction({val[0], kleeneNot(val[1])}); },
   },
   {
      .name = "nested",
      .filter = [](
                   const auto& arg
                ) { return fmt::format("{} && !({} || !({}))", arg[0], arg[1], arg[2]); },
      .expected =
         [](const auto& val) {
            return conjunction({val[0], kleeneNot(disjunction({val[1], kleeneNot(val[2])}))});
         },
   },
   {
      .name = "orOfNegations",
      .filter = [](const auto& arg) { return fmt::format("!({}) || !({})", arg[0], arg[1]); },
      .expected =
         [](const auto& val) { return disjunction({kleeneNot(val[0]), kleeneNot(val[1])}); },
   },
   {
      .name = "atLeastTwoOf",
      .filter = [](
                   const auto& arg
                ) { return fmt::format("nOf(2, {{{}, {}, {}}})", arg[0], arg[1], arg[2]); },
      .expected = [](const auto& val) { return nOf(val, 2, false); },
   },
   {
      .name = "atLeastOneOfWithNegation",
      .filter = [](
                   const auto& arg
                ) { return fmt::format("nOf(1, {{{}, !({}), {}}})", arg[0], arg[1], arg[2]); },
      .expected =
         [](const auto& val) { return nOf({val[0], kleeneNot(val[1]), val[2]}, 1, false); },
   },
   {
      .name = "exactlyOneOf",
      .filter =
         [](const auto& arg) {
            return fmt::format(
               "nOf(1, {{{}, {}, {}}}, matchExactly:=true)", arg[0], arg[1], arg[2]
            );
         },
      .expected = [](const auto& val) { return nOf(val, 1, true); },
   },
   {
      .name = "exactlyTwoOfWithNegation",
      .filter =
         [](const auto& arg) {
            return fmt::format(
               "nOf(2, {{{}, !({}), {}}}, matchExactly:=true)", arg[0], arg[1], arg[2]
            );
         },
      .expected = [](const auto& val) { return nOf({val[0], kleeneNot(val[1]), val[2]}, 2, true); },
   },
};

nlohmann::json rowsWhere(const std::function<bool(const std::vector<Truth>&)>& predicate) {
   nlohmann::json result = nlohmann::json::array();
   for (uint32_t row = 0; row < NUM_ROWS; ++row) {
      const std::vector<Truth> inputs{inputValue(0, row), inputValue(1, row), inputValue(2, row)};
      if (predicate(inputs)) {
         result.push_back({{"primaryKey", primaryKey(row)}});
      }
   }
   return result;
}

/// For each template and each choice of atoms, one scenario for the filter (selecting its true
/// rows) and one for its negation (selecting its false rows)
std::vector<QueryTestScenario> scenarios() {
   std::vector<QueryTestScenario> result;
   for (const auto& filter_template : TEMPLATES) {
      for (size_t atom = 0; atom < ATOMS.size(); ++atom) {
         std::vector<std::string> atoms;
         atoms.reserve(NUM_INPUTS);
         for (uint32_t input = 0; input < NUM_INPUTS; ++input) {
            atoms.push_back(
               fmt::format(
                  fmt::runtime(ATOMS.at((atom + (static_cast<size_t>(input) * 3)) % ATOMS.size())),
                  input
               )
            );
         }
         const std::string filter = filter_template.filter(atoms);
         const auto& expected = filter_template.expected;
         result.push_back({
            .name = fmt::format("{}_{}", filter_template.name, atom),
            .query = fmt::format("data.filter({}).project({{primaryKey}})", filter),
            .expected_query_result =
               rowsWhere([&](const auto& inputs) { return expected(inputs) == Truth::TRUE_VALUE; }),
         });
         result.push_back({
            .name = fmt::format("{}_{}_negated", filter_template.name, atom),
            .query = fmt::format("data.filter(!({})).project({{primaryKey}})", filter),
            .expected_query_result = rowsWhere([&](const auto& inputs) {
               return expected(inputs) == Truth::FALSE_VALUE;
            }),
         });
      }
   }
   return result;
}

}  // namespace

QUERY_TEST(ThreeValuedLogic, TEST_DATA, ::testing::ValuesIn(scenarios()));
