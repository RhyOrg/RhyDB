#include "rhydb/roaring_util/roaring_container.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <roaring/roaring.hh>

#include "rhydb/roaring_util/bitmap_builder.h"

using rhydb::roaring_util::BitmapBuilderByContainer;
using rhydb::roaring_util::CopyOnWriteContainer;
using rhydb::roaring_util::RoaringContainer;
using rhydb::roaring_util::RoaringContainerView;

namespace {

// Reconstructs the set of low-16-bit values held by a container by materializing it into a
// single-container roaring bitmap.
roaring::Roaring toRoaring(const RoaringContainer& container) {
   BitmapBuilderByContainer builder;
   builder.addContainer(0, container.rawContainer(), container.getTypecode());
   return std::move(builder).getBitmap();
}

// Collects the low-16-bit values yielded by iterating a view, in iteration order.
std::vector<uint16_t> iterate(const RoaringContainerView& view) {
   std::vector<uint16_t> values;
   for (const uint16_t value : view) {
      values.push_back(value);
   }
   return values;
}

// Builds an owning container holding exactly `values`. An empty list yields an empty container
// (cardinality 0), which the set-algebra tests use to exercise the empty-operand paths.
RoaringContainer makeContainer(std::initializer_list<uint16_t> values) {
   auto container =
      RoaringContainer::withCapacity(static_cast<int32_t>(std::max(values.size(), std::size_t{1})));
   for (const uint16_t value : values) {
      container.add(value);
   }
   return container;
}

}  // namespace

TEST(RoaringContainer, addAndCardinality) {
   auto container = RoaringContainer::withCapacity(4);
   EXPECT_TRUE(container.empty());

   container.add(3);
   container.add(7);
   container.add(9);

   EXPECT_EQ(container.getCardinality(), 3);
   EXPECT_FALSE(container.empty());
   EXPECT_EQ(toRoaring(container), (roaring::Roaring{3, 7, 9}));
}

TEST(RoaringContainer, growsFromArrayToBitset) {
   // A small capacity starts as an array container and must transparently grow past the array
   // container limit into a bitset as values are added.
   auto container = RoaringContainer::withCapacity(1);
   const uint16_t count = 20000;
   for (uint16_t value = 0; value < count; ++value) {
      container.add(value);
   }
   EXPECT_EQ(container.getCardinality(), count);
   roaring::Roaring expected;
   expected.addRange(0, count);
   EXPECT_EQ(toRoaring(container), expected);
}

TEST(RoaringContainer, clonedFromIsIndependent) {
   auto source = RoaringContainer::withCapacity(4);
   source.add(1);
   source.add(2);

   auto clone = RoaringContainer::clonedFrom(source.rawContainer(), source.getTypecode());
   EXPECT_EQ(clone.getCardinality(), 2);

   // Mutating the clone must not affect the source.
   clone.add(3);
   EXPECT_EQ(clone.getCardinality(), 3);
   EXPECT_EQ(source.getCardinality(), 2);
}

TEST(RoaringContainer, moveTransfersOwnership) {
   auto container = RoaringContainer::withCapacity(4);
   container.add(5);

   auto moved = std::move(container);
   EXPECT_EQ(moved.getCardinality(), 1);
   EXPECT_EQ(toRoaring(moved), roaring::Roaring{5});
}

TEST(RoaringContainer, runOptimizeAndShrinkPreservesContents) {
   auto container = RoaringContainer::withCapacity(4);
   for (uint16_t value = 100; value < 200; ++value) {
      container.add(value);
   }
   const auto before = toRoaring(container);
   container.runOptimizeAndShrink();
   EXPECT_EQ(container.getCardinality(), 100);
   EXPECT_EQ(toRoaring(container), before);
}

TEST(RoaringContainerView, mirrorsOwnerAndDoesNotFree) {
   auto owner = RoaringContainer::withCapacity(4);
   owner.add(3);
   owner.add(7);

   const RoaringContainerView view{owner};
   EXPECT_EQ(view.getCardinality(), 2);
   EXPECT_FALSE(view.empty());
   EXPECT_EQ(view.rawContainer(), owner.rawContainer());
   EXPECT_EQ(view.getTypecode(), owner.getTypecode());

   // The view is non-owning: when it goes out of scope the owner's container stays valid.
   {
      const RoaringContainerView scoped{owner};
      EXPECT_EQ(scoped.getCardinality(), 2);
   }
   EXPECT_EQ(toRoaring(owner), (roaring::Roaring{3, 7}));
}

TEST(RoaringContainerView, toOwningDeepCopies) {
   auto owner = RoaringContainer::withCapacity(4);
   owner.add(1);
   owner.add(2);

   const RoaringContainerView view{owner};
   RoaringContainer owned = view.toOwning();
   EXPECT_EQ(owned.getCardinality(), 2);

   // Mutating the deep copy must not affect the original owner.
   owned.add(3);
   EXPECT_EQ(owned.getCardinality(), 3);
   EXPECT_EQ(owner.getCardinality(), 2);
   EXPECT_EQ(toRoaring(owner), (roaring::Roaring{1, 2}));
}

TEST(RoaringContainerView, iteratesArrayContainerValuesAscending) {
   auto owner = RoaringContainer::withCapacity(4);
   owner.add(3);
   owner.add(7);
   owner.add(9);

   const RoaringContainerView view{owner};
   EXPECT_EQ(iterate(view), (std::vector<uint16_t>{3, 7, 9}));
}

TEST(RoaringContainerView, iteratesBitsetContainerValuesAscending) {
   // Enough values to force the array container to grow into a bitset container.
   auto owner = RoaringContainer::withCapacity(1);
   const uint16_t count = 20000;
   for (uint16_t value = 0; value < count; ++value) {
      owner.add(value);
   }
   ASSERT_EQ(owner.getTypecode(), BITSET_CONTAINER_TYPE);

   const auto values = iterate(RoaringContainerView{owner});
   ASSERT_EQ(values.size(), count);
   for (uint16_t value = 0; value < count; ++value) {
      EXPECT_EQ(values[value], value);
   }
}

TEST(RoaringContainerView, iteratesRunContainerValuesAscending) {
   // A dense contiguous range run-optimizes into a run container.
   auto owner = RoaringContainer::withCapacity(4);
   for (uint16_t value = 100; value < 200; ++value) {
      owner.add(value);
   }
   owner.runOptimizeAndShrink();
   ASSERT_EQ(owner.getTypecode(), RUN_CONTAINER_TYPE);

   const auto values = iterate(RoaringContainerView{owner});
   ASSERT_EQ(values.size(), 100);
   for (uint16_t offset = 0; offset < 100; ++offset) {
      EXPECT_EQ(values[offset], 100 + offset);
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(RoaringContainerView, iteratorsAtDifferentValuesOfOneRunCompareUnequal) {
   // A single contiguous run: every value of the container lives in run index 0, so an iterator
   // position is only distinguishable by its current value, not by the internal run index.
   auto owner = RoaringContainer::withCapacity(4);
   for (uint16_t value = 100; value < 200; ++value) {
      owner.add(value);
   }
   owner.runOptimizeAndShrink();
   ASSERT_EQ(owner.getTypecode(), RUN_CONTAINER_TYPE);

   const RoaringContainerView view{owner};
   auto first = view.begin();
   auto second = view.begin();
   ++second;

   ASSERT_EQ(*first, 100);
   ASSERT_EQ(*second, 101);
   EXPECT_NE(first, second);
   EXPECT_FALSE(first == second);

   // Advancing a copy must not move the original, and equality must hold again once the copy has
   // been caught up.
   ++first;
   EXPECT_EQ(first, second);
   EXPECT_EQ(*first, *second);

   // Every position within the run must be distinct from every other one.
   for (auto left = view.begin(); left != RoaringContainerView::end(); ++left) {
      for (auto right = view.begin(); right != RoaringContainerView::end(); ++right) {
         EXPECT_EQ(left == right, *left == *right);
      }
   }
}

TEST(RoaringContainerView, algorithmsTerminateCorrectlyWithinOneRun) {
   // Standard algorithms compare iterators for equality; if positions inside a run collapsed onto
   // one another they would stop early or miscount.
   auto owner = RoaringContainer::withCapacity(4);
   for (uint16_t value = 100; value < 200; ++value) {
      owner.add(value);
   }
   owner.runOptimizeAndShrink();
   ASSERT_EQ(owner.getTypecode(), RUN_CONTAINER_TYPE);

   const RoaringContainerView view{owner};
   EXPECT_EQ(std::distance(view.begin(), RoaringContainerView::end()), 100);

   const auto found = std::find(view.begin(), RoaringContainerView::end(), uint16_t{150});
   ASSERT_NE(found, RoaringContainerView::end());
   EXPECT_EQ(*found, 150);
   EXPECT_EQ(std::distance(view.begin(), found), 50);

   EXPECT_EQ(
      std::find(view.begin(), RoaringContainerView::end(), uint16_t{250}),
      RoaringContainerView::end()
   );
}

TEST(RoaringContainerView, emptyViewIteratesAsEmptyRange) {
   const auto owner = RoaringContainer::withCapacity(4);
   const RoaringContainerView view{owner};
   ASSERT_TRUE(view.empty());
   EXPECT_EQ(view.begin(), view.end());
   EXPECT_TRUE(iterate(view).empty());
}

TEST(RoaringContainer, serializationRoundTrip) {
   auto container = RoaringContainer::withCapacity(4);
   container.add(1);
   container.add(42);
   container.add(1000);
   const auto expected = toRoaring(container);

   std::stringstream stream;
   {
      boost::archive::binary_oarchive output_archive(stream);
      output_archive << container;
   }

   // How to silence the deprecation warning when one needs to default construct the object
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
   RoaringContainer restored;
#pragma GCC diagnostic pop
   {
      boost::archive::binary_iarchive input_archive(stream);
      input_archive >> restored;
   }

   EXPECT_EQ(restored.getCardinality(), 3);
   EXPECT_EQ(toRoaring(restored), expected);
}

TEST(RoaringContainerSetAlgebra, intersectionKeepsCommonValues) {
   const auto lhs = makeContainer({1, 2, 3, 5, 8});
   const auto rhs = makeContainer({2, 3, 5, 7});

   const RoaringContainer result = RoaringContainerView{lhs} & RoaringContainerView{rhs};
   EXPECT_EQ(result.getCardinality(), 3);
   EXPECT_EQ(toRoaring(result), (roaring::Roaring{2, 3, 5}));
}

TEST(RoaringContainerSetAlgebra, intersectionOfDisjointContainersIsEmpty) {
   const auto lhs = makeContainer({1, 3, 5});
   const auto rhs = makeContainer({2, 4, 6});

   const RoaringContainer result = RoaringContainerView{lhs} & RoaringContainerView{rhs};
   EXPECT_TRUE(result.empty());
   EXPECT_EQ(result.getCardinality(), 0);
   // An empty result must still be a valid owning container, not a container-less object.
   EXPECT_NE(result.rawContainer(), nullptr);
}

TEST(RoaringContainerSetAlgebra, intersectionWithEmptyOperandIsEmpty) {
   const auto values = makeContainer({1, 2, 3});
   const auto empty = makeContainer({});

   const RoaringContainer empty_rhs = RoaringContainerView{values} & RoaringContainerView{empty};
   EXPECT_TRUE(empty_rhs.empty());
   EXPECT_NE(empty_rhs.rawContainer(), nullptr);

   const RoaringContainer empty_lhs = RoaringContainerView{empty} & RoaringContainerView{values};
   EXPECT_TRUE(empty_lhs.empty());
   EXPECT_NE(empty_lhs.rawContainer(), nullptr);
}

TEST(RoaringContainerSetAlgebra, differenceRemovesRhsValues) {
   const auto lhs = makeContainer({1, 2, 3, 4, 5});
   const auto rhs = makeContainer({2, 4});

   const RoaringContainer result = RoaringContainerView{lhs} - RoaringContainerView{rhs};
   EXPECT_EQ(toRoaring(result), (roaring::Roaring{1, 3, 5}));
}

TEST(RoaringContainerSetAlgebra, differenceWithEmptyRhsCopiesLhs) {
   const auto lhs = makeContainer({1, 2, 3});
   const auto empty = makeContainer({});

   const RoaringContainer result = RoaringContainerView{lhs} - RoaringContainerView{empty};
   EXPECT_EQ(toRoaring(result), (roaring::Roaring{1, 2, 3}));
   // The result is an independent owning copy, not a borrow of `lhs`.
   EXPECT_NE(result.rawContainer(), lhs.rawContainer());
}

TEST(RoaringContainerSetAlgebra, differenceWithEmptyLhsIsEmpty) {
   const auto empty = makeContainer({});
   const auto rhs = makeContainer({1, 2, 3});

   const RoaringContainer result = RoaringContainerView{empty} - RoaringContainerView{rhs};
   EXPECT_TRUE(result.empty());
   EXPECT_NE(result.rawContainer(), nullptr);
}

TEST(RoaringContainerSetAlgebra, differenceOfEqualContainersIsEmpty) {
   // The C-API difference produces an empty container here; the "no empty containers" invariant
   // requires it to come back as an empty owning container, not a kept zero-cardinality one.
   const auto values = makeContainer({1, 2, 3});

   const RoaringContainer result = RoaringContainerView{values} - RoaringContainerView{values};
   EXPECT_TRUE(result.empty());
   EXPECT_EQ(result.getCardinality(), 0);
   EXPECT_NE(result.rawContainer(), nullptr);
}

TEST(RoaringContainerSetAlgebra, unionMergesValues) {
   const auto lhs = makeContainer({1, 3, 5});
   const auto rhs = makeContainer({2, 4, 6});

   const RoaringContainer result = RoaringContainerView{lhs} | RoaringContainerView{rhs};
   EXPECT_EQ(toRoaring(result), (roaring::Roaring{1, 2, 3, 4, 5, 6}));
}

TEST(RoaringContainerSetAlgebra, unionWithEmptyOperandsReturnsTheOther) {
   const auto values = makeContainer({1, 2, 3});
   const auto empty = makeContainer({});

   EXPECT_EQ(
      toRoaring(RoaringContainerView{empty} | RoaringContainerView{values}),
      (roaring::Roaring{1, 2, 3})
   );
   EXPECT_EQ(
      toRoaring(RoaringContainerView{values} | RoaringContainerView{empty}),
      (roaring::Roaring{1, 2, 3})
   );
   const RoaringContainer both_empty = RoaringContainerView{empty} | RoaringContainerView{empty};
   EXPECT_TRUE(both_empty.empty());
   EXPECT_NE(both_empty.rawContainer(), nullptr);
}

TEST(RoaringContainerSetAlgebra, orAssignAccumulatesInPlace) {
   RoaringContainer accumulator = makeContainer({1, 2});
   const auto addend = makeContainer({2, 3, 4});

   accumulator |= RoaringContainerView{addend};
   EXPECT_EQ(toRoaring(accumulator), (roaring::Roaring{1, 2, 3, 4}));
}

TEST(RoaringContainerSetAlgebra, orAssignIntoEmptyAccumulatorTakesAddend) {
   RoaringContainer accumulator = makeContainer({});
   const auto addend = makeContainer({5, 6});

   accumulator |= RoaringContainerView{addend};
   EXPECT_EQ(toRoaring(accumulator), (roaring::Roaring{5, 6}));
}

TEST(RoaringContainerSetAlgebra, orAssignEmptyAddendLeavesAccumulatorUnchanged) {
   RoaringContainer accumulator = makeContainer({1, 2});
   const auto empty = makeContainer({});

   accumulator |= RoaringContainerView{empty};
   EXPECT_EQ(toRoaring(accumulator), (roaring::Roaring{1, 2}));
}

TEST(RoaringContainerSetAlgebra, orAssignSelfLeavesContainerUnchanged) {
   RoaringContainer accumulator = makeContainer({1, 2, 3});

   accumulator |= RoaringContainerView{accumulator};
   EXPECT_EQ(toRoaring(accumulator), (roaring::Roaring{1, 2, 3}));
}

// The array-array union spills into a bitset once the result exceeds the array container limit, so
// the roaring API hands back a freshly allocated container instead of unioning in place.
TEST(RoaringContainerSetAlgebra, orAssignGrowingBeyondArrayCapacityConvertsToBitset) {
   RoaringContainer accumulator = makeContainer({});
   roaring::Roaring expected;
   for (uint16_t value = 0; value < 3000; ++value) {
      accumulator.add(value);
      expected.add(value);
   }
   RoaringContainer addend = makeContainer({});
   for (uint16_t value = 3000; value < 6000; ++value) {
      addend.add(value);
      expected.add(value);
   }
   ASSERT_EQ(accumulator.getTypecode(), ARRAY_CONTAINER_TYPE);
   ASSERT_EQ(addend.getTypecode(), ARRAY_CONTAINER_TYPE);

   accumulator |= RoaringContainerView{addend};

   EXPECT_EQ(accumulator.getTypecode(), BITSET_CONTAINER_TYPE);
   EXPECT_EQ(accumulator.getCardinality(), 6000);
   EXPECT_EQ(toRoaring(accumulator), expected);
}

// An array accumulator cannot absorb a bitset addend in place either.
TEST(RoaringContainerSetAlgebra, orAssignArrayAccumulatorWithBitsetAddend) {
   RoaringContainer accumulator = makeContainer({1, 60000});
   RoaringContainer addend = makeContainer({});
   roaring::Roaring expected{1, 60000};
   for (uint16_t value = 0; value < 5000; ++value) {
      addend.add(value);
      expected.add(value);
   }
   ASSERT_EQ(accumulator.getTypecode(), ARRAY_CONTAINER_TYPE);
   ASSERT_EQ(addend.getTypecode(), BITSET_CONTAINER_TYPE);

   accumulator |= RoaringContainerView{addend};

   EXPECT_EQ(toRoaring(accumulator), expected);
}

// Run containers union in place, but the result is re-converted to whichever representation is most
// compact, which again may replace the container.
TEST(RoaringContainerSetAlgebra, orAssignRunContainers) {
   RoaringContainer accumulator = makeContainer({});
   RoaringContainer addend = makeContainer({});
   roaring::Roaring expected;
   for (uint16_t value = 0; value < 5000; ++value) {
      accumulator.add(value);
      expected.add(value);
   }
   for (uint16_t value = 4000; value < 9000; ++value) {
      addend.add(value);
      expected.add(value);
   }
   accumulator.runOptimizeAndShrink();
   addend.runOptimizeAndShrink();
   ASSERT_EQ(accumulator.getTypecode(), RUN_CONTAINER_TYPE);
   ASSERT_EQ(addend.getTypecode(), RUN_CONTAINER_TYPE);

   accumulator |= RoaringContainerView{addend};

   EXPECT_EQ(accumulator.getCardinality(), 9000);
   EXPECT_EQ(toRoaring(accumulator), expected);
}

TEST(RoaringContainerSetAlgebra, orAssignAccumulatesRepeatedly) {
   RoaringContainer accumulator = makeContainer({});
   roaring::Roaring expected;
   for (uint16_t offset = 0; offset < 100; ++offset) {
      const RoaringContainer addend = makeContainer({offset, static_cast<uint16_t>(offset + 100)});
      expected.add(offset);
      expected.add(static_cast<uint16_t>(offset + 100));
      accumulator |= RoaringContainerView{addend};
   }

   EXPECT_EQ(toRoaring(accumulator), expected);
}

TEST(CopyOnWriteContainer, borrowsWithoutCopying) {
   auto owner = makeContainer({3, 7, 9});
   const CopyOnWriteContainer cow{RoaringContainerView{owner}};

   // While only reading, it is a zero-copy view: same underlying container, same cardinality.
   EXPECT_EQ(cow.getCardinality(), 3);
   EXPECT_FALSE(cow.empty());
   EXPECT_EQ(cow.view().rawContainer(), owner.rawContainer());
}

TEST(CopyOnWriteContainer, unionClonesOnWriteAndLeavesSourceUnchanged) {
   auto owner = makeContainer({1, 2, 3});
   const auto* borrowed = owner.rawContainer();
   CopyOnWriteContainer cow{RoaringContainerView{owner}};
   const auto addend = makeContainer({4});

   cow |= RoaringContainerView{addend};

   // The copy-on-write produced a private owning copy: the borrowed source is untouched ...
   EXPECT_EQ(toRoaring(owner), (roaring::Roaring{1, 2, 3}));
   EXPECT_EQ(owner.rawContainer(), borrowed);
   // ... and the container now owns a different container holding the union.
   EXPECT_NE(cow.view().rawContainer(), borrowed);
   EXPECT_EQ(toRoaring(cow.toOwning()), (roaring::Roaring{1, 2, 3, 4}));
}

TEST(CopyOnWriteContainer, differenceClonesOnWriteAndLeavesSourceUnchanged) {
   auto owner = makeContainer({1, 2, 3});
   CopyOnWriteContainer cow{RoaringContainerView{owner}};

   cow -= RoaringContainerView{makeContainer({2})};

   EXPECT_EQ(toRoaring(owner), (roaring::Roaring{1, 2, 3}));
   EXPECT_EQ(toRoaring(cow.toOwning()), (roaring::Roaring{1, 3}));
}

TEST(CopyOnWriteContainer, intersectionClonesOnWriteAndLeavesSourceUnchanged) {
   auto owner = makeContainer({1, 2, 3});
   CopyOnWriteContainer cow{RoaringContainerView{owner}};

   cow &= RoaringContainerView{makeContainer({2, 3, 4})};

   EXPECT_EQ(toRoaring(owner), (roaring::Roaring{1, 2, 3}));
   EXPECT_EQ(toRoaring(cow.toOwning()), (roaring::Roaring{2, 3}));
}

TEST(CopyOnWriteContainer, defaultIsEmptyAndUnionTakesAddend) {
   CopyOnWriteContainer cow;
   EXPECT_TRUE(cow.empty());

   cow |= RoaringContainerView{makeContainer({5, 6})};

   EXPECT_EQ(toRoaring(cow.toOwning()), (roaring::Roaring{5, 6}));
}

// Roaring's mixed array/run union reads element 0 of the array operand before checking its
// cardinality. An empty array's buffer holds a stale value there; if it lies below the run it used
// to be merged into the result as a spurious row. Pin the stale slot to 0 so this is deterministic.
TEST(RoaringContainer, unionIntoEmptyArrayIgnoresStaleSlot) {
   auto* array = roaring::internal::array_container_create_given_capacity(1);
   array->array[0] = 0;
   RoaringContainer target{array, 0, ARRAY_CONTAINER_TYPE};
   const RoaringContainer run{
      roaring::internal::run_container_create_range(5, 50), 45, RUN_CONTAINER_TYPE
   };

   target |= RoaringContainerView{run};

   EXPECT_EQ(target.getCardinality(), 45);
   roaring::Roaring expected;
   expected.addRange(5, 50);
   EXPECT_EQ(toRoaring(target), expected);
}

TEST(RoaringContainer, unionWithEmptyArrayAddendLeavesRunUnchanged) {
   RoaringContainer target{
      roaring::internal::run_container_create_range(5, 50), 45, RUN_CONTAINER_TYPE
   };
   auto* array = roaring::internal::array_container_create_given_capacity(1);
   array->array[0] = 0;
   const RoaringContainer empty_addend{array, 0, ARRAY_CONTAINER_TYPE};

   target |= RoaringContainerView{empty_addend};

   EXPECT_EQ(target.getCardinality(), 45);
   roaring::Roaring expected;
   expected.addRange(5, 50);
   EXPECT_EQ(toRoaring(target), expected);
}

TEST(CopyOnWriteContainer, defaultUnionWithRunTakesExactlyTheRun) {
   const RoaringContainer run{
      roaring::internal::run_container_create_range(5, 50), 45, RUN_CONTAINER_TYPE
   };
   CopyOnWriteContainer cow;

   cow |= RoaringContainerView{run};

   EXPECT_EQ(cow.getCardinality(), 45);
   roaring::Roaring expected;
   expected.addRange(5, 50);
   EXPECT_EQ(toRoaring(cow.toOwning()), expected);
}

TEST(CopyOnWriteContainer, defaultViewsAProperlyConstructedEmptyContainer) {
   const CopyOnWriteContainer cow;

   // The default borrows a real, properly-constructed empty container -- not a null handle -- so
   // reads are all well-defined.
   EXPECT_TRUE(cow.empty());
   EXPECT_EQ(cow.getCardinality(), 0);
   EXPECT_NE(cow.view().rawContainer(), nullptr);
   EXPECT_EQ(cow.begin(), cow.end());  // iterates as an empty range
}

TEST(CopyOnWriteContainer, defaultToOwningIsAValidEmptyContainer) {
   const CopyOnWriteContainer cow;

   // toOwning on the default clones the shared empty container into an independent, valid empty one
   // (a real allocation, safe to mutate afterwards) rather than dereferencing a null handle.
   RoaringContainer owned = cow.toOwning();
   EXPECT_TRUE(owned.empty());
   EXPECT_NE(owned.rawContainer(), nullptr);
   owned.add(5);
   EXPECT_EQ(toRoaring(owned), roaring::Roaring{5});
}

TEST(CopyOnWriteContainer, defaultDifferenceAndIntersectionAreSafe) {
   CopyOnWriteContainer cow;

   cow -= RoaringContainerView{makeContainer({1, 2})};  // empty - x stays empty
   EXPECT_TRUE(cow.empty());

   cow &= RoaringContainerView{makeContainer({1, 2})};  // empty & x stays empty
   EXPECT_TRUE(cow.empty());
   EXPECT_NE(cow.toOwning().rawContainer(), nullptr);
}

TEST(CopyOnWriteContainer, distinctDefaultsShareTheEmptyContainerButOwnIndependentlyOnWrite) {
   CopyOnWriteContainer first;
   const CopyOnWriteContainer second;

   // Both defaults borrow the same shared empty container.
   EXPECT_EQ(first.view().rawContainer(), second.view().rawContainer());

   // Writing to one copy-on-writes into its own container, leaving the other (and the shared empty)
   // untouched.
   first |= RoaringContainerView{makeContainer({7})};
   EXPECT_EQ(toRoaring(first.toOwning()), roaring::Roaring{7});
   EXPECT_TRUE(second.empty());
   EXPECT_NE(first.view().rawContainer(), second.view().rawContainer());
}

TEST(CopyOnWriteContainer, unionWithEmptyIsANoOp) {
   CopyOnWriteContainer cow{makeContainer({1, 2})};
   const auto empty = makeContainer({});

   cow |= RoaringContainerView{empty};

   EXPECT_EQ(toRoaring(cow.toOwning()), (roaring::Roaring{1, 2}));
}

TEST(CopyOnWriteContainer, takesOwnershipAndMutatesInPlace) {
   CopyOnWriteContainer cow{makeContainer({1, 2})};

   cow |= RoaringContainerView{makeContainer({2, 3})};

   EXPECT_EQ(toRoaring(cow.toOwning()), (roaring::Roaring{1, 2, 3}));
}

TEST(CopyOnWriteContainer, copyOfBorrowingReborrowsSameContainer) {
   auto owner = makeContainer({1, 2});
   const CopyOnWriteContainer cow{RoaringContainerView{owner}};

   const CopyOnWriteContainer copy = cow;  // NOLINT(performance-unnecessary-copy-initialization)

   // A borrowing copy re-borrows the same underlying container rather than cloning it.
   EXPECT_EQ(copy.view().rawContainer(), owner.rawContainer());
}

TEST(CopyOnWriteContainer, copyOfOwningDeepCopiesAndIsIndependent) {
   const CopyOnWriteContainer owned{makeContainer({1, 2})};
   CopyOnWriteContainer copy = owned;

   // Mutating the copy must not affect the original owning container.
   copy |= RoaringContainerView{makeContainer({3})};

   EXPECT_EQ(toRoaring(owned.toOwning()), (roaring::Roaring{1, 2}));
   EXPECT_EQ(toRoaring(copy.toOwning()), (roaring::Roaring{1, 2, 3}));
   EXPECT_NE(owned.view().rawContainer(), copy.view().rawContainer());
}

TEST(CopyOnWriteContainer, toOwningDeepCopiesTheBorrowedContainer) {
   auto owner = makeContainer({1, 2});
   const CopyOnWriteContainer cow{RoaringContainerView{owner}};

   RoaringContainer owned = cow.toOwning();
   owned.add(3);

   // The extracted container is independent of the borrowed source.
   EXPECT_EQ(toRoaring(owner), (roaring::Roaring{1, 2}));
   EXPECT_EQ(toRoaring(owned), (roaring::Roaring{1, 2, 3}));
}

TEST(CopyOnWriteContainer, iteratesCurrentContentAscending) {
   const CopyOnWriteContainer cow{makeContainer({9, 3, 7})};

   std::vector<uint16_t> values;
   for (const uint16_t value : cow) {
      values.push_back(value);
   }
   EXPECT_EQ(values, (std::vector<uint16_t>{3, 7, 9}));
}
