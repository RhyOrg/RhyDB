#include "rhydb/query_engine/filter/operators/intersection.h"

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/query_compilation_exception.h"

using rhydb::Bitmap;
using rhydb::query_engine::QueryCompilationException;
using rhydb::query_engine::filter::operators::IndexScan;
using rhydb::query_engine::filter::operators::Intersection;
using rhydb::query_engine::filter::operators::OperatorVector;
using rhydb::storage::column::RowLayout;

namespace {
OperatorVector generateTestInput(
   const std::vector<roaring::Roaring>& bitmaps,
   const RowLayout& row_layout
) {
   OperatorVector result;

   std::ranges::transform(bitmaps, std::back_inserter(result), [&](const auto& bitmap) {
      return std::make_unique<IndexScan>(Bitmap{&bitmap}, row_layout);
   });
   return result;
}
}  // namespace

TEST(OperatorIntersection, shouldFailOnEmptyInput) {
   OperatorVector non_negated;
   OperatorVector negated;
   const auto row_layout = RowLayout::of(5);

   ASSERT_THROW(
      const Intersection under_test(std::move(non_negated), std::move(negated), row_layout),
      QueryCompilationException
   );
}

TEST(OperatorIntersection, shouldFailOnOnlyNegated) {
   const std::vector<roaring::Roaring> test_negated_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated;
   OperatorVector negated = generateTestInput(test_negated_bitmaps, row_layout);
   ASSERT_THROW(
      const Intersection under_test(std::move(non_negated), std::move(negated), row_layout),
      QueryCompilationException
   );
}

TEST(OperatorIntersection, shouldFailOnOneNonNegated) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring({1, 2, 3})}});
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated = generateTestInput(test_bitmaps, row_layout);
   OperatorVector negated;
   ASSERT_THROW(
      const Intersection under_test(std::move(non_negated), std::move(negated), row_layout),
      std::runtime_error
   );
}

TEST(OperatorIntersection, evaluateShouldReturnCorrectValuesNoNegated) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated = generateTestInput(test_bitmaps, row_layout);
   OperatorVector negated;
   const Intersection under_test(std::move(non_negated), std::move(negated), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1, 3}));
}

TEST(OperatorIntersection, evaluateShouldReturnCorrectValues) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const std::vector<roaring::Roaring> test_negated_bitmaps(
      {{roaring::Roaring(), roaring::Roaring({3})}}
   );
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated = generateTestInput(test_bitmaps, row_layout);
   OperatorVector negated = generateTestInput(test_negated_bitmaps, row_layout);
   const Intersection under_test(std::move(non_negated), std::move(negated), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1}));
}

TEST(OperatorIntersection, evaluateShouldReturnCorrectValuesManyNegated) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring({1, 2, 3})}});
   const std::vector<roaring::Roaring> test_negated_bitmaps({{
      roaring::Roaring(),
      roaring::Roaring({3}),
      roaring::Roaring({4}),
      roaring::Roaring({2, 4}),
   },});
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated = generateTestInput(test_bitmaps, row_layout);
   OperatorVector negated = generateTestInput(test_negated_bitmaps, row_layout);
   const Intersection under_test(std::move(non_negated), std::move(negated), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring({1}));
}

TEST(OperatorIntersection, evaluateShouldReturnCorrectValuesEmptyInput) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring()}});
   const std::vector<roaring::Roaring> test_negated_bitmaps({{
      roaring::Roaring({3}),
      roaring::Roaring({4}),
      roaring::Roaring({2, 4}),
   },});
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated = generateTestInput(test_bitmaps, row_layout);
   OperatorVector negated = generateTestInput(test_negated_bitmaps, row_layout);
   const Intersection under_test(std::move(non_negated), std::move(negated), row_layout);
   ASSERT_EQ(under_test.evaluate().toRoaring(), roaring::Roaring());
}

TEST(OperatorIntersection, correctTypeInfo) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(5);

   OperatorVector non_negated = generateTestInput(test_bitmaps, row_layout);
   OperatorVector negated;
   const Intersection under_test(std::move(non_negated), std::move(negated), row_layout);

   ASSERT_EQ(under_test.type(), rhydb::query_engine::filter::operators::INTERSECTION);
}
