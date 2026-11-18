#include "rhydb/query_engine/scalar_expressions/string_in_set.h"

#include <sstream>

#include <arrow/api.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <roaring/roaring.hh>

#include "rhydb/query_engine/scalar_expressions/negation.h"
#include "rhydb/test/query_fixture.test.h"
#include "rhydb/test/scalar_expression_test_util.test.h"

namespace {
using rhydb::ReferenceGenomes;
using rhydb::test::QueryTestData;
using rhydb::test::QueryTestScenario;

nlohmann::json createData(const std::string& primary_key, const std::string& country) {
   return nlohmann::json::parse(
      fmt::format(
         R"(
{{
   "primaryKey": "{}",
   "country": "{}",
   "region": "{}",
   "segment1": {{
      "sequence": "ACGT",
      "insertions": []
   }}
}}
)",
         primary_key,
         country,
         country == "USA" ? "Americas" : "Europe"
      )
   );
}

const auto DATABASE_CONFIG =
   R"(
schema:
  instanceName: "dummy name"
  metadata:
   - name: "primaryKey"
     type: "string"
   - name: "country"
     type: "string"
   - name: "region"
     type: "string"
     generateIndex: true
  primaryKey: "primaryKey"
)";

const auto REFERENCE_GENOMES = ReferenceGenomes{
   {{"segment1", "ATGCN"}},
   {},
};

const QueryTestData TEST_DATA{
   .ndjson_input_data =
      {
         createData("id_0", "Switzerland"),
         createData("id_1", "Germany"),
         createData("id_2", "USA"),
         createData("id_3", "Switzerland"),
         createData("id_4", "France"),
         createData("id_5", "Germany"),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

const QueryTestScenario STRING_IN_SET_SINGLE_VALUE = {
   .name = "STRING_IN_SET_SINGLE_VALUE",
   .query = "data.filter(country.in({'Switzerland'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0"},{"country":"Switzerland","primaryKey":"id_3"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_MULTIPLE_VALUES = {
   .name = "STRING_IN_SET_MULTIPLE_VALUES",
   .query = "data.filter(country.in({'Switzerland', 'Germany'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0"},{"country":"Germany","primaryKey":"id_1"},{"country":"Switzerland","primaryKey":"id_3"},{"country":"Germany","primaryKey":"id_5"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_NO_MATCH = {
   .name = "STRING_IN_SET_NO_MATCH",
   .query = "data.filter(country.in({'Japan', 'China'})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

const QueryTestScenario STRING_IN_SET_EMPTY_VALUES = {
   .name = "STRING_IN_SET_EMPTY_VALUES",
   .query = "data.filter(country.in({})).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(R"([])"),
};

const QueryTestScenario STRING_IN_SET_INDEXED_COLUMN = {
   .name = "STRING_IN_SET_INDEXED_COLUMN",
   .query = "data.filter(region.in({'Europe'})).project({primaryKey, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"primaryKey":"id_0","region":"Europe"},{"primaryKey":"id_1","region":"Europe"},{"primaryKey":"id_3","region":"Europe"},{"primaryKey":"id_4","region":"Europe"},{"primaryKey":"id_5","region":"Europe"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_WITH_AND = {
   .name = "STRING_IN_SET_WITH_AND",
   .query =
      "data.filter(country.in({'Switzerland', 'Germany', 'France'}) && region = "
      "'Europe').project({primaryKey, country, region})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"Switzerland","primaryKey":"id_0","region":"Europe"},{"country":"Germany","primaryKey":"id_1","region":"Europe"},{"country":"Switzerland","primaryKey":"id_3","region":"Europe"},{"country":"France","primaryKey":"id_4","region":"Europe"},{"country":"Germany","primaryKey":"id_5","region":"Europe"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_NEGATED = {
   .name = "STRING_IN_SET_NEGATED",
   .query = "data.filter(!(country.in({'Switzerland', 'Germany'}))).project({primaryKey, country})",
   .expected_query_result = nlohmann::json::parse(
      R"([{"country":"USA","primaryKey":"id_2"},{"country":"France","primaryKey":"id_4"}])"
   ),
};

const QueryTestScenario STRING_IN_SET_INVALID_COLUMN_TYPE_INTEGER = {
   .name = "STRING_IN_SET_INVALID_COLUMN_TYPE_INTEGER",
   .query = "data.filter(in(123, {'Switzerland'}))",
   .expected_error_message = "expected identifier at 1:16",
};

const QueryTestScenario STRING_IN_SET_INVALID_COLUMN_TYPE_STRING = {
   .name = "STRING_IN_SET_INVALID_COLUMN_TYPE_STRING",
   .query = "data.filter('string'.in({'Switzerland'}))",
   .expected_error_message = "expected identifier at 1:13",
};

const QueryTestScenario STRING_IN_SET_INVALID_VALUES_TYPE = {
   .name = "STRING_IN_SET_INVALID_VALUES_TYPE",
   .query = "data.filter(country.in('Switzerland'))",
   .expected_error_message =
      "in() expects a set literal (e.g. {'a', 'b'}) or a subquery (e.g. "
      "data.filter(...).project({column})), got 'Switzerland' at 1:24",
};

const QueryTestScenario STRING_IN_SET_NONEXISTENT_COLUMN = {
   .name = "STRING_IN_SET_NONEXISTENT_COLUMN",
   .query = "data.filter(nonexistent.in({'Switzerland'}))",
   .expected_error_message = "The database does not contain the column 'nonexistent'",
};

const QueryTestData TEST_DATA_WITH_NULL{
   .ndjson_input_data =
      {
         nlohmann::json::parse(
            R"({"primaryKey": "id_0", "country": "Switzerland", "region": "Europe", "segment1": null})"
         ),
         nlohmann::json::parse(
            R"({"primaryKey": "id_1", "country": "", "region": "", "segment1": null})"
         ),
         nlohmann::json::parse(
            R"({"primaryKey": "id_2", "country": null, "region": null, "segment1": null})"
         ),
      },
   .database_config = DATABASE_CONFIG,
   .reference_genomes = REFERENCE_GENOMES,
   .without_unaligned_sequences = true,
};

const QueryTestScenario STRING_IN_SET_EMPTY_STRING_DOES_NOT_MATCH_NULL = {
   .name = "STRING_IN_SET_EMPTY_STRING_DOES_NOT_MATCH_NULL",
   .query = "data.filter(country.in({'', 'Germany'})).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

const QueryTestScenario STRING_IN_SET_EMPTY_STRING_DOES_NOT_MATCH_NULL_INDEXED_COLUMN = {
   .name = "STRING_IN_SET_EMPTY_STRING_DOES_NOT_MATCH_NULL_INDEXED_COLUMN",
   .query = "data.filter(region.in({'', 'Americas'})).project(primaryKey)",
   .expected_query_result = nlohmann::json::parse(R"([{"primaryKey":"id_1"}])"),
};

}  // namespace

QUERY_TEST(
   StringInSet,
   TEST_DATA,
   ::testing::Values(
      STRING_IN_SET_SINGLE_VALUE,
      STRING_IN_SET_MULTIPLE_VALUES,
      STRING_IN_SET_NO_MATCH,
      STRING_IN_SET_EMPTY_VALUES,
      STRING_IN_SET_INDEXED_COLUMN,
      STRING_IN_SET_WITH_AND,
      STRING_IN_SET_NEGATED,
      STRING_IN_SET_INVALID_COLUMN_TYPE_INTEGER,
      STRING_IN_SET_INVALID_COLUMN_TYPE_STRING,
      STRING_IN_SET_INVALID_VALUES_TYPE,
      STRING_IN_SET_NONEXISTENT_COLUMN
   )
);

QUERY_TEST(
   StringInSetWithNull,
   TEST_DATA_WITH_NULL,
   ::testing::Values(
      STRING_IN_SET_EMPTY_STRING_DOES_NOT_MATCH_NULL,
      STRING_IN_SET_EMPTY_STRING_DOES_NOT_MATCH_NULL_INDEXED_COLUMN
   )
);

namespace {

std::shared_ptr<rhydb::Database> createDatabaseWithNull() {
   auto database = std::make_shared<rhydb::Database>();
   rhydb::initialize::Initializer::createTableInDatabase(
      rhydb::schema::TableName::getDefault(),
      rhydb::config::DatabaseConfig::getValidatedConfig(TEST_DATA_WITH_NULL.database_config),
      TEST_DATA_WITH_NULL.reference_genomes,
      TEST_DATA_WITH_NULL.lineage_trees,
      TEST_DATA_WITH_NULL.phylo_tree_file,
      TEST_DATA_WITH_NULL.without_unaligned_sequences,
      *database
   );
   std::stringstream ndjson;
   for (const auto& object : TEST_DATA_WITH_NULL.ndjson_input_data) {
      ndjson << object.dump() << "\n";
   }
   database->appendData(rhydb::schema::TableName::getDefault(), ndjson);
   return database;
}

roaring::Roaring selectedRowsOfEmptySet(const std::string& column_name, bool with_null_rows) {
   const auto database = createDatabaseWithNull();
   const auto& table = *database->tables.at(rhydb::schema::TableName::getDefault());
   const rhydb::query_engine::scalar_expressions::StringInSet string_in_set(
      {.name = column_name, .type = rhydb::schema::DataType::STRING}, {}
   );
   return string_in_set
      .rewrite(table, rhydb::query_engine::scalar_expressions::ScalarExpression::NONE)
      ->compile(table, with_null_rows)
      ->evaluate()
      .toRoaring();
}

}  // namespace

// The empty set is matched by no row, so it is false (not null) on the null rows. Both storage
// paths must agree: the non-indexed column compiles to a row-wise selection, the indexed one is
// rewritten to an empty `Or`.
TEST(StringInSetCompile, emptySetDoesNotSelectNullRowsOfNonIndexedColumn) {
   EXPECT_EQ(selectedRowsOfEmptySet("country", false), roaring::Roaring{});
   EXPECT_EQ(selectedRowsOfEmptySet("country", true), roaring::Roaring{});
}

TEST(StringInSetCompile, emptySetDoesNotSelectNullRowsOfIndexedColumn) {
   EXPECT_EQ(selectedRowsOfEmptySet("region", false), roaring::Roaring{});
   EXPECT_EQ(selectedRowsOfEmptySet("region", true), roaring::Roaring{});
}

namespace {

using rhydb::query_engine::scalar_expressions::Negation;
using rhydb::query_engine::scalar_expressions::StringInSet;
using rhydb::test::evaluateOnBatch;
using rhydb::test::makeBooleanArray;
using rhydb::test::makeLargeStringArray;

const auto STRING_COLUMN = rhydb::schema::ColumnIdentifier{
   .name = "country",
   .type = rhydb::schema::DataType::STRING,
};

std::shared_ptr<arrow::RecordBatch> batchWithCountries(
   const std::vector<std::optional<std::string>>& countries
) {
   return arrow::RecordBatch::Make(
      arrow::schema({arrow::field("country", arrow::large_utf8())}),
      static_cast<int64_t>(countries.size()),
      {makeLargeStringArray(countries)}
   );
}

void expectEvaluatesTo(
   const rhydb::query_engine::scalar_expressions::ScalarExpression& expression,
   const std::vector<std::optional<std::string>>& countries,
   const std::vector<std::optional<bool>>& expected
) {
   const auto batch = batchWithCountries(countries);
   const auto actual = evaluateOnBatch(expression, batch->schema(), batch);
   EXPECT_TRUE(actual->Equals(*makeBooleanArray(expected)))
      << "actual: " << actual->ToString() << "expected: " << makeBooleanArray(expected)->ToString();
}

}  // namespace

// There is no query test for the Arrow translation: a `StringInSet` only comes into existence in
// `rewrite`, which the Arrow path (`FilterNode`) does not call. A SaneQL `in` is parsed into
// comparisons, which are translated on their own.
TEST(StringInSetArrow, isTrueForMembersFalseForOthersAndNullForNull) {
   expectEvaluatesTo(
      StringInSet(STRING_COLUMN, {"Switzerland", "Germany"}),
      {"Switzerland", "USA", std::nullopt, "Germany", ""},
      {true, false, std::nullopt, true, false}
   );
}

TEST(StringInSetArrow, emptyStringIsAMemberOnlyIfItIsInTheSet) {
   expectEvaluatesTo(
      StringInSet(STRING_COLUMN, {""}), {"", "USA", std::nullopt}, {true, false, std::nullopt}
   );
}

// Like the bitmap path, the empty set is false on every row, including the null ones.
TEST(StringInSetArrow, emptySetIsFalseOnEveryRow) {
   expectEvaluatesTo(StringInSet(STRING_COLUMN, {}), {"Switzerland", std::nullopt}, {false, false});
}

TEST(StringInSetArrow, negationDoesNotSelectNullRows) {
   expectEvaluatesTo(
      Negation(
         std::make_unique<StringInSet>(STRING_COLUMN, std::unordered_set<std::string>{"Germany"})
      ),
      {"Germany", "USA", std::nullopt},
      {false, true, std::nullopt}
   );
}
