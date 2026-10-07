#include "rhydb/query_engine/filter/operators/string_in_set.h"

#include <unordered_set>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/string_column.h"

using rhydb::query_engine::filter::operators::Selection;
using rhydb::query_engine::filter::operators::StringInSet;
using rhydb::storage::column::DictionaryEncodedColumn;
using rhydb::storage::column::DictionaryEncodedColumnMetadata;
using rhydb::storage::column::RowId;
using rhydb::storage::column::RowLayout;
using rhydb::storage::column::StringColumn;
using rhydb::storage::column::StringColumnMetadata;

namespace {

std::pair<std::shared_ptr<StringColumnMetadata>, StringColumn> makeTestStringColumn(
   const std::vector<std::string>& values
) {
   auto metadata = std::make_shared<StringColumnMetadata>("test");
   StringColumn test_column{metadata.get()};
   StringColumn::Builder builder;
   for (const auto& value : values) {
      builder.insert(value);
   }
   RHYDB_ASSERT(test_column.appendChunk(builder.finalize()).has_value());
   return {metadata, std::move(test_column)};
}

std::pair<std::shared_ptr<DictionaryEncodedColumnMetadata>, DictionaryEncodedColumn>
makeTestDictionaryEncodedColumn(const std::vector<std::string>& values) {
   auto metadata = std::make_shared<DictionaryEncodedColumnMetadata>("test_indexed");
   DictionaryEncodedColumn test_column{metadata.get()};
   DictionaryEncodedColumn::Builder builder;
   for (const auto& value : values) {
      builder.insert(value);
   }
   RHYDB_ASSERT(test_column.appendChunk(builder.finalize()).has_value());
   return {metadata, std::move(test_column)};
}

}  // namespace

TEST(OperatorStringInSet, matchReturnsCorrectValuesForStringColumn) {
   const std::vector<std::string> values{
      {"Switzerland", "Germany", "USA", "Switzerland", "France", "Germany"},
   };
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column,
         StringInSet<StringColumn>::Comparator::IN,
         std::unordered_set<std::string>{"Switzerland", "Germany"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 1, 3, 5}));
}

TEST(OperatorStringInSet, matchReturnsCorrectValuesForDictionaryEncodedColumn) {
   const std::vector<std::string> values{
      {"Switzerland", "Germany", "USA", "Switzerland", "France", "Germany"},
   };
   auto [metadata, test_column] = makeTestDictionaryEncodedColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<DictionaryEncodedColumn>>(
         &test_column,
         StringInSet<DictionaryEncodedColumn>::Comparator::IN,
         std::unordered_set<std::string>{"Switzerland", "Germany"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 1, 3, 5}));
}

TEST(OperatorStringInSet, matchReturnsEmptyForNoMatches) {
   const std::vector<std::string> values{
      {"Switzerland", "Germany", "USA", "Switzerland", "France", "Germany"},
   };
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column,
         StringInSet<StringColumn>::Comparator::IN,
         std::unordered_set<std::string>{"Japan", "China"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorStringInSet, matchReturnsEmptyForEmptySet) {
   const std::vector<std::string> values{
      {"Switzerland", "Germany", "USA", "Switzerland", "France", "Germany"},
   };
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column, StringInSet<StringColumn>::Comparator::IN, std::unordered_set<std::string>{}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorStringInSet, negationWorksCorrectly) {
   const std::vector<std::string> values{
      {"Switzerland", "Germany", "USA", "Switzerland", "France", "Germany"},
   };
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column,
         StringInSet<StringColumn>::Comparator::IN,
         std::unordered_set<std::string>{"Switzerland", "Germany"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 1, 3, 5}));

   auto negated = Selection::negate(std::move(under_test));
   ASSERT_EQ(negated->evaluate().toRoaring(), roaring::Roaring({2, 4}));
}

TEST(OperatorStringInSet, notInComparatorWorksCorrectly) {
   const std::vector<std::string> values{
      {"Switzerland", "Germany", "USA", "Switzerland", "France", "Germany"},
   };
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column,
         StringInSet<StringColumn>::Comparator::NOT_IN,
         std::unordered_set<std::string>{"Switzerland", "Germany"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({2, 4}));
}

TEST(OperatorStringInSet, toStringReturnsCorrectFormat) {
   const std::vector<std::string> values{"Switzerland", "Germany"};
   auto [metadata, test_column] = makeTestStringColumn(values);

   const StringInSet<StringColumn> in_predicate(
      &test_column,
      StringInSet<StringColumn>::Comparator::IN,
      std::unordered_set<std::string>{"Value"}
   );

   ASSERT_EQ(in_predicate.toString(), "test IN [Value]");

   const StringInSet<StringColumn> not_in_predicate(
      &test_column,
      StringInSet<StringColumn>::Comparator::NOT_IN,
      std::unordered_set<std::string>{"Value"}
   );

   ASSERT_EQ(not_in_predicate.toString(), "test NOT IN [Value]");
}

TEST(OperatorStringInSet, copyCreatesIndependentCopy) {
   const std::vector<std::string> values{"Switzerland", "Germany"};
   auto [metadata, test_column] = makeTestStringColumn(values);

   auto original = std::make_unique<StringInSet<StringColumn>>(
      &test_column,
      StringInSet<StringColumn>::Comparator::IN,
      std::unordered_set<std::string>{"Switzerland"}
   );

   auto copy = original->copy();

   ASSERT_EQ(original->toString(), copy->toString());
   ASSERT_EQ(original->match(RowId::fromGlobal(0)), copy->match(RowId::fromGlobal(0)));
   ASSERT_EQ(original->match(RowId::fromGlobal(1)), copy->match(RowId::fromGlobal(1)));
}

TEST(OperatorStringInSet, matchSingleValue) {
   const std::vector<std::string> values{"Apple", "Banana", "Cherry", "Apple", "Date"};
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   auto under_test = std::make_unique<Selection>(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column,
         StringInSet<StringColumn>::Comparator::IN,
         std::unordered_set<std::string>{"Apple"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test->evaluate().toRoaring(), roaring::Roaring({0, 3}));
}

TEST(OperatorStringInSet, returnsCorrectTypeInfo) {
   const std::vector<std::string> values{"Switzerland"};
   auto [metadata, test_column] = makeTestStringColumn(values);
   const auto row_layout = RowLayout::of(values.size());

   const Selection under_test(
      std::make_unique<StringInSet<StringColumn>>(
         &test_column,
         StringInSet<StringColumn>::Comparator::IN,
         std::unordered_set<std::string>{"Switzerland"}
      ),
      row_layout
   );

   ASSERT_EQ(under_test.type(), rhydb::query_engine::filter::operators::SELECTION);
}
