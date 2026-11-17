#include "rhydb/query_engine/filter/operators/threshold.h"

#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/query_compilation_exception.h"

using rhydb::Bitmap;
using rhydb::query_engine::filter::operators::IndexScan;
using rhydb::query_engine::filter::operators::OperatorVector;
using rhydb::query_engine::filter::operators::Threshold;
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

TEST(OperatorThreshold, evaluatesCorrectOnEmptyInput) {
   OperatorVector non_negated;
   OperatorVector negated;

   ASSERT_THROW(
      const Threshold under_test(
         std::move(non_negated), std::move(negated), 2, true, RowLayout::of()
      ),
      rhydb::query_engine::QueryCompilationException
   );
}

TEST(OperatorThreshold, evaluatesCorrectOnlyNegated) {
   const std::vector<roaring::Roaring> test_negated_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      OperatorVector(), generateTestInput(test_negated_bitmaps, row_layout), 1, true, row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({2}));

   const Threshold under_test_1_or_more(
      OperatorVector(), generateTestInput(test_negated_bitmaps, row_layout), 1, false, row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 2}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValuesNoNegated) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 1, true, row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 2, true, row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({2, 3}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 1, false, row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 2, false, row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValues) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const std::vector<roaring::Roaring> test_negated_bitmaps(
      {{roaring::Roaring(), roaring::Roaring({3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({0}));

   const Threshold under_test_3_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_3_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_4_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_4_exact.evaluate().toRoaring(), roaring::Roaring({2, 3}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_3_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_3_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));

   const Threshold under_test_4_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_4_or_more.evaluate().toRoaring(), roaring::Roaring({1, 2, 3}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValuesManyNegated) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring({1, 2, 3})}});
   const std::vector<roaring::Roaring> test_negated_bitmaps({
      {
         roaring::Roaring(),
         roaring::Roaring({3}),
         roaring::Roaring({4}),
         roaring::Roaring({2, 4}),
      },
   });
   const auto row_layout = RowLayout::of(5);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({4}));

   const Threshold under_test_3_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_3_exact.evaluate().toRoaring(), roaring::Roaring({}));

   const Threshold under_test_4_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_4_exact.evaluate().toRoaring(), roaring::Roaring({0, 2, 3}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4}));

   const Threshold under_test_3_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_3_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_4_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      4,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_4_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));
}

TEST(OperatorThreshold, evaluateShouldReturnCorrectValuesEmptyInput) {
   const std::vector<roaring::Roaring> test_bitmaps({{roaring::Roaring()}});
   const std::vector<roaring::Roaring> test_negated_bitmaps({
      {
         roaring::Roaring({3}),
         roaring::Roaring({4}),
         roaring::Roaring({2, 4}),
      },
   });
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test_1_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_1_exact.evaluate().toRoaring(), roaring::Roaring({4}));

   const Threshold under_test_2_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_2_exact.evaluate().toRoaring(), roaring::Roaring({2, 3}));

   const Threshold under_test_3_exact(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      true,
      row_layout
   );
   ASSERT_EQ(under_test_3_exact.evaluate().toRoaring(), roaring::Roaring({0, 1}));

   const Threshold under_test_1_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      1,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_1_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3, 4}));

   const Threshold under_test_2_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      2,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_2_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1, 2, 3}));

   const Threshold under_test_3_or_more(
      generateTestInput(test_bitmaps, row_layout),
      generateTestInput(test_negated_bitmaps, row_layout),
      3,
      false,
      row_layout
   );
   ASSERT_EQ(under_test_3_or_more.evaluate().toRoaring(), roaring::Roaring({0, 1}));
}

TEST(OperatorThreshold, correctTypeInfo) {
   const std::vector<roaring::Roaring> test_bitmaps(
      {{roaring::Roaring({1, 2, 3}), roaring::Roaring({1, 2, 3})}}
   );
   const auto row_layout = RowLayout::of(4);

   const Threshold under_test(
      generateTestInput(test_bitmaps, row_layout), OperatorVector(), 1, true, row_layout
   );

   ASSERT_EQ(under_test.type(), rhydb::query_engine::filter::operators::THRESHOLD);
}

namespace {
/// Rows of `row_layout`, each contained with probability `density`
roaring::Roaring randomRows(const RowLayout& row_layout, double density, std::mt19937& random) {
   std::bernoulli_distribution contains(density);
   roaring::Roaring result;
   for (const auto row_id : row_layout) {
      if (contains(random)) {
         result.add(row_id.toGlobal());
      }
   }
   return result;
}

/// The rows of `row_layout` contained in at least (or, if `match_exactly`, exactly)
/// `number_of_matchers` of the `non_negated` sets and complements of the `negated` sets
roaring::Roaring countMatches(
   const RowLayout& row_layout,
   const std::vector<roaring::Roaring>& non_negated,
   const std::vector<roaring::Roaring>& negated,
   uint32_t number_of_matchers,
   bool match_exactly
) {
   roaring::Roaring result;
   for (const auto row_id : row_layout) {
      const uint32_t row = row_id.toGlobal();
      uint32_t count = 0;
      for (const auto& rows : non_negated) {
         count += rows.contains(row) ? 1 : 0;
      }
      for (const auto& rows : negated) {
         count += rows.contains(row) ? 0 : 1;
      }
      if (match_exactly ? count == number_of_matchers : count >= number_of_matchers) {
         result.add(row);
      }
   }
   return result;
}
}  // namespace

// Sparse and empty children leave most entries of the dynamic program empty, which the evaluation
// skips. Compare against counting every row, over several chunks.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(OperatorThreshold, matchesCountingEveryRowForSparseAndEmptyChildren) {
   const auto row_layout = RowLayout::of(300, 70, 150);
   // NOLINTNEXTLINE(bugprone-random-generator-seed)
   std::mt19937 random{42};
   for (const double density : {0.0, 0.01, 0.1, 0.5, 0.95, 1.0}) {
      for (size_t non_negated_count = 0; non_negated_count <= 4; ++non_negated_count) {
         for (size_t negated_count = 0; negated_count <= 3; ++negated_count) {
            const size_t child_count = non_negated_count + negated_count;
            std::vector<roaring::Roaring> non_negated;
            non_negated.reserve(non_negated_count);
            std::vector<roaring::Roaring> negated;
            negated.reserve(negated_count);
            for (size_t idx = 0; idx < non_negated_count; ++idx) {
               // Some children are empty, whatever the density
               non_negated.push_back(
                  idx == 1 ? roaring::Roaring{} : randomRows(row_layout, density, random)
               );
            }
            for (size_t idx = 0; idx < negated_count; ++idx) {
               negated.push_back(
                  idx == 1 ? roaring::Roaring{} : randomRows(row_layout, density, random)
               );
            }
            for (uint32_t number_of_matchers = 1; number_of_matchers < child_count;
                 ++number_of_matchers) {
               for (const bool match_exactly : {false, true}) {
                  const Threshold under_test(
                     generateTestInput(non_negated, row_layout),
                     generateTestInput(negated, row_layout),
                     number_of_matchers,
                     match_exactly,
                     row_layout
                  );
                  EXPECT_EQ(
                     under_test.evaluate().toRoaring(),
                     countMatches(
                        row_layout, non_negated, negated, number_of_matchers, match_exactly
                     )
                  ) << "density "
                    << density << ", " << non_negated_count << " non-negated, " << negated_count
                    << " negated, " << number_of_matchers
                    << (match_exactly ? " exactly" : " or more");
               }
            }
         }
      }
   }
}
