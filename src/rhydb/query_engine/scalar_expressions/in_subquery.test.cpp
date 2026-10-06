#include <string>

#include <nlohmann/json.hpp>

#include "rhydb/test/query_fixture.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(
   const std::string& primary_key,
   const std::string& country,
   const std::string& region,
   int32_t year
) {
   return {{"primaryKey", primary_key}, {"country", country}, {"region", region}, {"year", year}};
}

// `country` is dictionary-encoded (generateIndex), so `country.in(<subquery>)` exercises the
// per-value bitmap union path; `region` is a plain string column; `year` is an int column used to
// show `in` is not restricted to string columns.
const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "test"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "country"
     type: "string"
     generateIndex: true
   - name: "region"
     type: "string"
   - name: "year"
     type: "int"
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{{}, {}};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_0", "Germany", "Europe", 2020),
         createData("id_1", "France", "Europe", 2021),
         createData("id_2", "Japan", "Asia", 2022),
         createData("id_3", "Germany", "Europe", 2020),
         createData("id_4", "Brazil", "SouthAmerica", 2023),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES
};

// The subquery yields a single country (Japan); the filter keeps rows whose country is in it.
const QueryTestScenario IN_SUBQUERY_SINGLE = {
   .name = "IN_SUBQUERY_SINGLE",
   .query =
      "data.filter(country.in(data.filter(region = 'Asia').project({country})))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_2"}])")
};

// The subquery yields {Germany, France}; matches all rows carrying either country.
const QueryTestScenario IN_SUBQUERY_MULTI = {
   .name = "IN_SUBQUERY_MULTI",
   .query =
      "data.filter(country.in(data.filter(region = 'Europe').project({country})))"
      ".project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_1"},{"primaryKey":"id_3"}])"
      )
};

// An empty subquery result matches no rows.
const QueryTestScenario IN_SUBQUERY_EMPTY = {
   .name = "IN_SUBQUERY_EMPTY",
   .query =
      "data.filter(country.in(data.filter(region = 'Antarctica').project({country})))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::array()
};

// in(<subquery>) is an ordinary scalar predicate, so it composes inside boolean expressions rather
// than only as a top-level filter: here `primaryKey = 'id_0'` OR `country in (Asian countries)`.
const QueryTestScenario IN_SUBQUERY_NESTED_IN_OR = {
   .name = "IN_SUBQUERY_NESTED_IN_OR",
   .query =
      "data.filter(primaryKey = 'id_0' || "
      "country.in(data.filter(region = 'Asia').project({country}))).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_2"}])")
};

// NOT: negating an in(<subquery>) keeps the rows whose value is not in the materialized set.
// {Japan} is excluded, so every non-Asian row remains.
const QueryTestScenario IN_SUBQUERY_NEGATED = {
   .name = "IN_SUBQUERY_NEGATED",
   .query =
      "data.filter(!(country.in(data.filter(region = 'Asia').project({country}))))"
      ".project({primaryKey})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_0"},{"primaryKey":"id_1"},{"primaryKey":"id_3"},{"primaryKey":"id_4"}])"
   )
};

// AND: in(<subquery>) combined with another predicate. Country in {Germany, France} and year 2020
// keeps only the two 2020 Germany rows.
const QueryTestScenario IN_SUBQUERY_NESTED_IN_AND = {
   .name = "IN_SUBQUERY_NESTED_IN_AND",
   .query =
      "data.filter(country.in(data.filter(region = 'Europe').project({country})) && "
      "year = 2020).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"}])")
};

// Two independent subqueries (over different column types) materialized in one predicate: country
// in {Brazil} OR year in {2022}.
const QueryTestScenario IN_SUBQUERY_OR_OF_TWO_SUBQUERIES = {
   .name = "IN_SUBQUERY_OR_OF_TWO_SUBQUERIES",
   .query =
      "data.filter(country.in(data.filter(region = 'SouthAmerica').project({country})) || "
      "year.in(data.filter(region = 'Asia').project({year}))).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_2"},{"primaryKey":"id_4"}])")
};

// The plain in(column, {set literal}) form must keep working alongside the subquery form.
const QueryTestScenario IN_SET_LITERAL_STILL_WORKS = {
   .name = "IN_SET_LITERAL_STILL_WORKS",
   .query = "data.filter(country.in({'Japan', 'Brazil'})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_2"},{"primaryKey":"id_4"}])")
};

// `in` is type-agnostic: a set literal of integers on an int column works.
const QueryTestScenario IN_SET_LITERAL_INT_COLUMN = {
   .name = "IN_SET_LITERAL_INT_COLUMN",
   .query = "data.filter(year.in({2020, 2023})).project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_3"},{"primaryKey":"id_4"}])"
      )
};

// A subquery over an int column: years occurring in Europe are {2020, 2021}, so any row with one of
// those years matches. Confirms the subquery path materializes non-string values.
const QueryTestScenario IN_SUBQUERY_INT_COLUMN = {
   .name = "IN_SUBQUERY_INT_COLUMN",
   .query =
      "data.filter(year.in(data.filter(region = 'Europe').project({year})))"
      ".project({primaryKey})",
   .expected_query_result =
      nlohmann::json::parse(R"([{"primaryKey":"id_0"},{"primaryKey":"id_1"},{"primaryKey":"id_3"}])"
      )
};

}  // namespace

QUERY_TEST(
   InSubqueryTest,
   TEST_DATA,
   ::testing::Values(
      IN_SUBQUERY_SINGLE,
      IN_SUBQUERY_MULTI,
      IN_SUBQUERY_EMPTY,
      IN_SUBQUERY_NESTED_IN_OR,
      IN_SUBQUERY_NEGATED,
      IN_SUBQUERY_NESTED_IN_AND,
      IN_SUBQUERY_OR_OF_TWO_SUBQUERIES,
      IN_SET_LITERAL_STILL_WORKS,
      IN_SET_LITERAL_INT_COLUMN,
      IN_SUBQUERY_INT_COLUMN
   )
)
