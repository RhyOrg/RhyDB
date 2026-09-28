#include "rhydb/storage/column/vertical_sequence_index.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <roaring/roaring.hh>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"

using rhydb::AminoAcid;
using rhydb::Bitmap;
using rhydb::Nucleotide;
using rhydb::SymbolMap;
using rhydb::storage::column::VerticalSequenceIndex;

class VerticalSequenceIndexTest : public ::testing::Test {
  protected:
   VerticalSequenceIndex<Nucleotide> index;

   void SetUp() override {
      // Fresh index for each test
      index = VerticalSequenceIndex<Nucleotide>();
   }
};

// Basic functionality tests
TEST_F(VerticalSequenceIndexTest, AddAndRetrieveSinglePosition) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids_per_symbol;
   ids_per_symbol[Nucleotide::Symbol::A] = {0, 2, 4};
   ids_per_symbol[Nucleotide::Symbol::C] = {1, 3};

   index.addSymbolsToPositions(0, ids_per_symbol);

   std::vector<std::string> sequences(5, "N");
   roaring::Roaring row_ids;
   for (uint32_t row_id = 0; row_id < 5; row_id++) {
      row_ids.add(row_id);
   }

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences[0], "A");
   EXPECT_EQ(sequences[1], "C");
   EXPECT_EQ(sequences[2], "A");
   EXPECT_EQ(sequences[3], "C");
   EXPECT_EQ(sequences[4], "A");
}

TEST_F(VerticalSequenceIndexTest, AddMultiplePositions) {
   const size_t num_seqs = 3;

   // Position 0
   SymbolMap<Nucleotide, std::vector<uint32_t>> pos0;
   pos0[Nucleotide::Symbol::A] = {0, 1};
   pos0[Nucleotide::Symbol::T] = {2};
   index.addSymbolsToPositions(0, pos0);

   // Position 1
   SymbolMap<Nucleotide, std::vector<uint32_t>> pos1;
   pos1[Nucleotide::Symbol::C] = {0};
   pos1[Nucleotide::Symbol::G] = {1, 2};
   index.addSymbolsToPositions(1, pos1);

   // Position 2
   SymbolMap<Nucleotide, std::vector<uint32_t>> pos2;
   pos2[Nucleotide::Symbol::T] = {0, 1, 2};
   index.addSymbolsToPositions(2, pos2);

   std::vector<std::string> sequences(num_seqs, "NNN");
   roaring::Roaring row_ids;
   for (uint32_t row_id = 0; row_id < num_seqs; row_id++) {
      row_ids.add(row_id);
   }

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences[0], "ACT");
   EXPECT_EQ(sequences[1], "AGT");
   EXPECT_EQ(sequences[2], "TGT");
}

TEST_F(VerticalSequenceIndexTest, SelectiveRowRetrieval) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> pos0;
   pos0[Nucleotide::Symbol::A] = {0, 1, 2, 3, 4};
   index.addSymbolsToPositions(0, pos0);

   SymbolMap<Nucleotide, std::vector<uint32_t>> pos1;
   pos1[Nucleotide::Symbol::C] = {0, 1, 2, 3, 4};
   index.addSymbolsToPositions(1, pos1);

   // Only retrieve rows 1 and 3
   std::vector<std::string> sequences(2, "GG");
   roaring::Roaring row_ids;
   row_ids.add(1);
   row_ids.add(3);

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences[0], "AC");
   EXPECT_EQ(sequences[1], "AC");
}

TEST_F(VerticalSequenceIndexTest, OverwriteExistingSequences) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::G] = {0, 1};
   index.addSymbolsToPositions(0, ids);

   std::vector<std::string> sequences = {"XXX", "YYY"};
   roaring::Roaring row_ids;
   row_ids.add(0);
   row_ids.add(1);

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences[0], "GXX");
   EXPECT_EQ(sequences[1], "GYY");
}

// Edge cases
TEST_F(VerticalSequenceIndexTest, EmptySymbolMap) {
   const SymbolMap<Nucleotide, std::vector<uint32_t>> empty_map;

   EXPECT_NO_THROW(index.addSymbolsToPositions(0, empty_map));

   std::vector<std::string> sequences(5, "C");
   roaring::Roaring row_ids;
   for (uint32_t row_id = 0; row_id < 5; row_id++) {
      row_ids.add(row_id);
   }

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   for (const auto& seq : sequences) {
      EXPECT_EQ(seq, "C");
   }
}

TEST_F(VerticalSequenceIndexTest, EmptyRowIds) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0, 1, 2};
   index.addSymbolsToPositions(0, ids);

   std::vector<std::string> no_sequences;
   const roaring::Roaring empty_row_ids;

   index.overwriteSymbolsInSequences(no_sequences, Bitmap{&empty_row_ids});

   ASSERT_TRUE(no_sequences.empty());
}

TEST_F(VerticalSequenceIndexTest, SingleSequence) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> pos0;
   pos0[Nucleotide::Symbol::A] = {0};
   index.addSymbolsToPositions(0, pos0);

   SymbolMap<Nucleotide, std::vector<uint32_t>> pos1;
   pos1[Nucleotide::Symbol::T] = {0};
   index.addSymbolsToPositions(1, pos1);

   std::vector<std::string> sequences(1, "TA");
   roaring::Roaring row_ids;
   row_ids.add(0);

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences[0], "AT");
}

TEST_F(VerticalSequenceIndexTest, LargeNumberOfSequences) {
   const size_t num_seqs = 70000;

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids_0;
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids_1;
   for (uint32_t row_id = 0; row_id < num_seqs; row_id++) {
      ids_0[Nucleotide::Symbol::A].push_back(row_id);
   }

   ids_1[Nucleotide::Symbol::G].push_back(0);
   ids_1[Nucleotide::Symbol::C].push_back(69999);

   index.addSymbolsToPositions(0, ids_0);
   index.addSymbolsToPositions(1, ids_1);

   std::vector<std::string> sequences(num_seqs, "NN");
   roaring::Roaring row_ids;
   for (uint32_t row_id = 0; row_id < num_seqs; row_id++) {
      row_ids.add(row_id);
   }

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences.at(0), "AG");

   for (size_t idx = 1; idx < 69999; idx++) {
      EXPECT_EQ(sequences.at(idx), "AN");
   }
   EXPECT_EQ(sequences.at(69999), "AC");
}

TEST_F(VerticalSequenceIndexTest, NonContiguousPositions) {
   // Add positions 0, 5, 10 (skipping intermediate positions)
   SymbolMap<Nucleotide, std::vector<uint32_t>> pos0;
   pos0[Nucleotide::Symbol::A] = {0};
   index.addSymbolsToPositions(0, pos0);

   SymbolMap<Nucleotide, std::vector<uint32_t>> pos5;
   pos5[Nucleotide::Symbol::C] = {0};
   index.addSymbolsToPositions(5, pos5);

   SymbolMap<Nucleotide, std::vector<uint32_t>> pos10;
   pos10[Nucleotide::Symbol::G] = {0};
   index.addSymbolsToPositions(10, pos10);

   std::vector<std::string> sequences(1, "01234567890");
   roaring::Roaring row_ids;
   row_ids.add(0);

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   EXPECT_EQ(sequences[0], "A1234C6789G");
}

TEST_F(VerticalSequenceIndexTest, AllDifferentNucleotideSymbols) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   uint32_t row_id = 0;
   for (const auto& symbol : Nucleotide::SYMBOLS) {
      ids[symbol] = {row_id++};
   }
   index.addSymbolsToPositions(0, ids);

   std::vector<std::string> sequences(Nucleotide::COUNT, "-");
   roaring::Roaring row_ids;
   row_ids.addRange(0, Nucleotide::COUNT);

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   row_id = 0;
   for (const auto& symbol : Nucleotide::SYMBOLS) {
      EXPECT_EQ(sequences[row_id], fmt::format("{}", Nucleotide::symbolToChar(symbol)));
      ++row_id;
   }
}

TEST(AminoAcidVerticalSequenceIndexTest, AllDifferentAminoAcidSymbols) {
   VerticalSequenceIndex<AminoAcid> index;
   SymbolMap<AminoAcid, std::vector<uint32_t>> ids;
   uint32_t row_id = 0;
   for (const auto& symbol : AminoAcid::SYMBOLS) {
      ids[symbol] = {row_id++};
   }
   index.addSymbolsToPositions(0, ids);

   std::vector<std::string> sequences(AminoAcid::COUNT, "-");
   roaring::Roaring row_ids;
   row_ids.addRange(0, AminoAcid::COUNT);

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   row_id = 0;
   for (const auto& symbol : AminoAcid::SYMBOLS) {
      EXPECT_EQ(sequences[row_id], fmt::format("{}", AminoAcid::symbolToChar(symbol)));
      ++row_id;
   }
}

TEST_F(VerticalSequenceIndexTest, SparseRowSelection) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   for (uint32_t row_id = 0; row_id < 100; row_id++) {
      if (row_id % 2 == 0) {
         ids[Nucleotide::Symbol::B].push_back(row_id);
      } else {
         ids[Nucleotide::Symbol::Y].push_back(row_id);
      }
   }
   index.addSymbolsToPositions(0, ids);

   // Select only even rows
   std::vector<std::string> sequences(100, "N");
   roaring::Roaring row_ids;
   for (uint32_t row_id = 0; row_id < 100; row_id++) {
      row_ids.add(row_id);
   }

   index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids});

   for (uint32_t row_id = 0; row_id < 100; row_id++) {
      if (row_id % 2 == 0) {
         EXPECT_EQ(sequences[row_id], "B");
      } else {
         EXPECT_EQ(sequences[row_id], "Y");
      }
   }
}

TEST_F(VerticalSequenceIndexTest, OutOfBoundsRowIds) {
   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0, 1, 2};
   index.addSymbolsToPositions(0, ids);

   std::vector<std::string> sequences(3, "N");
   roaring::Roaring row_ids;
   row_ids.add(0);
   row_ids.add(100);   // Out of bounds
   row_ids.add(1000);  // Out of bounds

   // Should handle gracefully without crash
   EXPECT_NO_THROW(index.overwriteSymbolsInSequences(sequences, Bitmap{&row_ids}));
}

TEST_F(VerticalSequenceIndexTest, referenceAdaptsSingleSymbol) {
   const uint32_t position = 0;

   // 1 sequence: [A]
   // Reference is C -> should be changed to A

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0};
   index.addSymbolsToPositions(position, ids);

   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::A}), roaring::Roaring{0}
   );

   auto adapted_reference = index.adaptLocalReference({0}, position, Nucleotide::Symbol::C);
   EXPECT_TRUE(adapted_reference.has_value());
   EXPECT_EQ(adapted_reference.value(), Nucleotide::Symbol::A);

   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::A}), roaring::Roaring{}
   );
   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::C}), roaring::Roaring{}
   );
}

TEST_F(VerticalSequenceIndexTest, referenceAdaptsSingleSymbolWhenSomeSymbolsAreMissing) {
   const uint32_t position = 0;

   // 3 sequences: [A, N, N]
   // Reference is C -> should be changed to A

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0};
   index.addSymbolsToPositions(position, ids);

   auto adapted_reference = index.adaptLocalReference({0}, position, Nucleotide::Symbol::C);
   EXPECT_TRUE(adapted_reference.has_value());
   EXPECT_EQ(adapted_reference.value(), Nucleotide::Symbol::A);

   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::A}), roaring::Roaring{}
   );
   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::C}), roaring::Roaring{}
   );
}

TEST_F(VerticalSequenceIndexTest, referenceDoesNotAdaptSingleSymbol) {
   const uint32_t position = 0;

   // 2 sequences: [A, implicit C]
   // Reference is C -> should not be changed

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0};
   index.addSymbolsToPositions(position, ids);

   auto adapted_reference = index.adaptLocalReference({0, 1}, position, Nucleotide::Symbol::C);
   EXPECT_FALSE(adapted_reference.has_value());
}

TEST_F(VerticalSequenceIndexTest, referenceDoesNotAdaptSingleSymbolWhenSomeSymbolsAreMissing) {
   const uint32_t position = 0;

   // 7 sequences: [A, N, implicit T, N, implicit T, implicit T, A]
   // Reference is T -> should not be changed

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0, 6};
   index.addSymbolsToPositions(position, ids);

   auto adapted_reference =
      index.adaptLocalReference({0, 2, 4, 5, 6}, position, Nucleotide::Symbol::T);
   EXPECT_FALSE(adapted_reference.has_value());
}

TEST_F(VerticalSequenceIndexTest, adaptsAndFlipsCorrectly) {
   const uint32_t position = 0;

   // 7 sequences: [A, N, implicit T, N, A, implicit T, A]
   // Reference is T -> should not be changed

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0, 4, 6};
   index.addSymbolsToPositions(position, ids);

   auto adapted_reference =
      index.adaptLocalReference({0, 2, 4, 5, 6}, position, Nucleotide::Symbol::T);
   EXPECT_TRUE(adapted_reference.has_value());
   EXPECT_EQ(adapted_reference.value(), Nucleotide::Symbol::A);

   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::A}), roaring::Roaring{}
   );
   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::T}),
      (roaring::Roaring{2, 5})
   );
}

TEST_F(VerticalSequenceIndexTest, adaptsAndFlipsCorrectlyWithManySymbols) {
   const uint32_t position = 0;

   // 10 sequences: [A, N, implicit T, N, A, -, -, -, implicit T, A]
   // Reference is T -> should not be changed

   SymbolMap<Nucleotide, std::vector<uint32_t>> ids;
   ids[Nucleotide::Symbol::A] = {0, 4, 9};
   ids[Nucleotide::Symbol::GAP] = {5, 6, 7};
   index.addSymbolsToPositions(position, ids);

   auto adapted_reference =
      index.adaptLocalReference({0, 2, 4, 5, 6, 7, 8, 9}, position, Nucleotide::Symbol::T);
   EXPECT_TRUE(adapted_reference.has_value());
   EXPECT_EQ(adapted_reference.value(), Nucleotide::Symbol::GAP);

   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::A}),
      (roaring::Roaring{0, 4, 9})
   );
   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::T}),
      (roaring::Roaring{2, 8})
   );
   EXPECT_EQ(
      index.getMatchingContainersAsBitmap(position, {Nucleotide::Symbol::GAP}), (roaring::Roaring{})
   );
}

using rhydb::storage::column::splitIdsIntoBatches;

TEST(splitIdsIntoBatches, EmptyVector) {
   const std::vector<uint32_t> input = {};
   auto result = splitIdsIntoBatches(input);

   EXPECT_TRUE(result.empty());
}

TEST(splitIdsIntoBatches, SingleElement) {
   const std::vector<uint32_t> input = {0x00010002};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 1);
   EXPECT_EQ(result[0].first, 0x0001);
   ASSERT_EQ(result[0].second.size(), 1);
   EXPECT_EQ(result[0].second[0], 0x0002);
}

TEST(splitIdsIntoBatches, SingleBatch) {
   // All IDs have the same upper 16 bits (0x0001)
   const std::vector<uint32_t> input = {0x00010001, 0x00010002, 0x00010003, 0x00010004};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 1);
   EXPECT_EQ(result[0].first, 0x0001);
   ASSERT_EQ(result[0].second.size(), 4);
   EXPECT_EQ(result[0].second[0], 0x0001);
   EXPECT_EQ(result[0].second[1], 0x0002);
   EXPECT_EQ(result[0].second[2], 0x0003);
   EXPECT_EQ(result[0].second[3], 0x0004);
}

TEST(splitIdsIntoBatches, MultipleBatches) {
   const std::vector<uint32_t> input = {
      0x00010001,
      0x00010002,  // Batch 1: upper bits = 0x0001
      0x00020001,
      0x00020002,
      0x00020003,  // Batch 2: upper bits = 0x0002
      0x00030001,   // Batch 3: upper bits = 0x0003
   };
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 3);

   // Batch 1
   EXPECT_EQ(result[0].first, 0x0001);
   ASSERT_EQ(result[0].second.size(), 2);
   EXPECT_EQ(result[0].second[0], 0x0001);
   EXPECT_EQ(result[0].second[1], 0x0002);

   // Batch 2
   EXPECT_EQ(result[1].first, 0x0002);
   ASSERT_EQ(result[1].second.size(), 3);
   EXPECT_EQ(result[1].second[0], 0x0001);
   EXPECT_EQ(result[1].second[1], 0x0002);
   EXPECT_EQ(result[1].second[2], 0x0003);

   // Batch 3
   EXPECT_EQ(result[2].first, 0x0003);
   ASSERT_EQ(result[2].second.size(), 1);
   EXPECT_EQ(result[2].second[0], 0x0001);
}

TEST(splitIdsIntoBatches, BoundaryValues) {
   const std::vector<uint32_t> input = {
      0x00000000,  // Minimum value
      0x0000FFFF,  // Maximum lower bits with 0 upper bits
      0xFFFF0000,  // Maximum upper bits with 0 lower bits
      0xFFFFFFFF,   // Maximum value
   };
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 2);

   // Batch 1: upper bits = 0x0000
   EXPECT_EQ(result[0].first, 0x0000);
   ASSERT_EQ(result[0].second.size(), 2);
   EXPECT_EQ(result[0].second[0], 0x0000);
   EXPECT_EQ(result[0].second[1], 0xFFFF);

   // Batch 2: upper bits = 0xFFFF
   EXPECT_EQ(result[1].first, 0xFFFF);
   ASSERT_EQ(result[1].second.size(), 2);
   EXPECT_EQ(result[1].second[0], 0x0000);
   EXPECT_EQ(result[1].second[1], 0xFFFF);
}

TEST(splitIdsIntoBatches, ConsecutiveBatches) {
   // Each batch has exactly one element
   const std::vector<uint32_t> input = {0x00010000, 0x00020000, 0x00030000, 0x00040000};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 4);
   for (size_t row_id = 0; row_id < result.size(); ++row_id) {
      EXPECT_EQ(result[row_id].first, row_id + 1);
      ASSERT_EQ(result[row_id].second.size(), 1);
      EXPECT_EQ(result[row_id].second[0], 0x0000);
   }
}

TEST(splitIdsIntoBatches, LargeGapInUpperBits) {
   const std::vector<uint32_t> input = {
      0x00010001,
      0x00010002,
      0x80000001,  // Large gap
      0x80000002,
   };
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 2);
   EXPECT_EQ(result[0].first, 0x0001);
   ASSERT_EQ(result[0].second.size(), 2);
   EXPECT_EQ(result[1].first, 0x8000);
   ASSERT_EQ(result[1].second.size(), 2);
}

TEST(splitIdsIntoBatches, DuplicateLowerBits) {
   // Same lower bits within a batch is valid
   const std::vector<uint32_t> input = {0x00010001, 0x00010001, 0x00010002, 0x00020001, 0x00020001};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 2);
   EXPECT_EQ(result[0].first, 0x0001);
   ASSERT_EQ(result[0].second.size(), 3);
   EXPECT_EQ(result[1].first, 0x0002);
   ASSERT_EQ(result[1].second.size(), 2);
}

TEST(splitIdsIntoBatches, ZeroUpperBits) {
   const std::vector<uint32_t> input = {0x00000001, 0x00000002, 0x00000003};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 1);
   EXPECT_EQ(result[0].first, 0x0000);
   ASSERT_EQ(result[0].second.size(), 3);
   EXPECT_EQ(result[0].second[0], 0x0001);
   EXPECT_EQ(result[0].second[1], 0x0002);
   EXPECT_EQ(result[0].second[2], 0x0003);
}

TEST(splitIdsIntoBatches, AllSameValue) {
   const std::vector<uint32_t> input = {0x12345678, 0x12345678, 0x12345678};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 1);
   EXPECT_EQ(result[0].first, 0x1234);
   ASSERT_EQ(result[0].second.size(), 3);
   EXPECT_EQ(result[0].second[0], 0x5678);
   EXPECT_EQ(result[0].second[1], 0x5678);
   EXPECT_EQ(result[0].second[2], 0x5678);
}

TEST(splitIdsIntoBatches, MaximumUpperBits) {
   const std::vector<uint32_t> input = {0xFFFF0001, 0xFFFF0002, 0xFFFFFFFF};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 1);
   EXPECT_EQ(result[0].first, 0xFFFF);
   ASSERT_EQ(result[0].second.size(), 3);
   EXPECT_EQ(result[0].second[0], 0x0001);
   EXPECT_EQ(result[0].second[1], 0x0002);
   EXPECT_EQ(result[0].second[2], 0xFFFF);
}

TEST(splitIdsIntoBatches, AlternatingBatches) {
   const std::vector<uint32_t> input = {0x00010001, 0x00020001, 0x00030001, 0x00040001};
   auto result = splitIdsIntoBatches(input);

   ASSERT_EQ(result.size(), 4);
   for (size_t row_id = 0; row_id < result.size(); ++row_id) {
      EXPECT_EQ(result[row_id].first, row_id + 1);
      ASSERT_EQ(result[row_id].second.size(), 1);
      EXPECT_EQ(result[row_id].second[0], 0x0001);
   }
}
