#include "rhydb/storage/column/insertion_index.h"

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/row_layout.h"

using rhydb::Nucleotide;
using rhydb::storage::column::RowId;
using rhydb::storage::insertion::InsertionIndex;
using rhydb::storage::insertion::InsertionSearchPattern;

TEST(InsertionIndex, searchesEveryChunk) {
   InsertionIndex<Nucleotide> under_test;
   under_test.add(10, "ACGTA", 0);
   under_test.add(10, "CC", 2);
   under_test.add(20, "ACGTA", 1);
   under_test.appendChunk();
   // A chunk without insertions
   under_test.appendChunk();
   under_test.add(10, "ACGTA", 1);
   under_test.add(10, "TTACGTAA", 3);
   under_test.appendChunk();

   ASSERT_EQ(under_test.getChunks().size(), 3);
   EXPECT_TRUE(under_test.getChunks()[1].getInsertionPositions().empty());

   const auto exact = InsertionSearchPattern<Nucleotide>::make("ACGTA");
   EXPECT_EQ(
      under_test.search(10, exact).toRoaring(),
      roaring::Roaring({RowId(0, 0).toGlobal(), RowId(2, 1).toGlobal()})
   );
   EXPECT_EQ(under_test.search(20, exact).toRoaring(), roaring::Roaring({RowId(0, 1).toGlobal()}));
   EXPECT_TRUE(under_test.search(30, exact).isEmpty());

   // Uses the three-mer index of every chunk
   const auto contained = InsertionSearchPattern<Nucleotide>::make(".*ACG.*");
   EXPECT_EQ(
      under_test.search(10, contained).toRoaring(),
      roaring::Roaring({RowId(0, 0).toGlobal(), RowId(2, 1).toGlobal(), RowId(2, 3).toGlobal()})
   );

   // Too short for a three-mer, so the regex is matched against every insertion
   const auto short_pattern = InsertionSearchPattern<Nucleotide>::make("CC");
   EXPECT_EQ(
      under_test.search(10, short_pattern).toRoaring(), roaring::Roaring({RowId(0, 2).toGlobal()})
   );
}

TEST(InsertionIndex, addingTheSameInsertionTwiceToARowCountsTheRowOnce) {
   InsertionIndex<Nucleotide> under_test;
   under_test.add(10, "ACG", 0);
   under_test.add(10, "ACG", 0);
   under_test.appendChunk();

   const auto& insertions = under_test.getChunks()[0].getInsertionPositions().at(10).insertions;
   ASSERT_EQ(insertions.size(), 1);
   EXPECT_EQ(insertions[0].row_ids.getCardinality(), 1);
}

TEST(InsertionIndex, deleteRowsCompactsEveryChunk) {
   InsertionIndex<Nucleotide> under_test;
   under_test.add(10, "ACGTA", 0);
   under_test.add(10, "ACGTA", 2);
   under_test.add(10, "CC", 1);
   under_test.appendChunk();
   under_test.add(10, "ACGTA", 0);
   under_test.appendChunk();
   under_test.add(10, "CC", 1);
   under_test.appendChunk();

   // The first chunk loses its row with "CC" and the one before it, the second chunk all its rows
   const rhydb::storage::column::RowCompaction compaction{
      rhydb::storage::column::RowLayout::of(3, 1, 2),
      rhydb::Bitmap{
         roaring::Roaring({RowId(0, 0).toGlobal(), RowId(0, 1).toGlobal(), RowId(1, 0).toGlobal()})
      },
   };
   under_test.deleteRows(compaction);

   ASSERT_EQ(under_test.getChunks().size(), 2);
   const auto exact = InsertionSearchPattern<Nucleotide>::make("ACGTA");
   EXPECT_EQ(under_test.search(10, exact).toRoaring(), roaring::Roaring({RowId(0, 0).toGlobal()}));
   const auto short_pattern = InsertionSearchPattern<Nucleotide>::make("CC");
   EXPECT_EQ(
      under_test.search(10, short_pattern).toRoaring(), roaring::Roaring({RowId(1, 1).toGlobal()})
   );
}
