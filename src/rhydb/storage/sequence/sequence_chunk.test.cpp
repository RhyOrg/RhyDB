#include "rhydb/storage/sequence/sequence_chunk.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/storage/column/row_compaction.h"
#include "rhydb/storage/column/row_layout.h"
#include "rhydb/storage/sequence/sequence_chunk_algorithms.h"

namespace rhydb::storage::sequence {

namespace {

using roaring_util::RoaringContainer;
using roaring_util::RoaringContainerView;

RoaringContainer containerOf(const std::vector<uint16_t>& rows) {
   auto result = RoaringContainer::withCapacity(static_cast<int32_t>(rows.size()));
   for (const uint16_t row : rows) {
      result.add(row);
   }
   return result;
}

std::vector<uint16_t> valuesOf(RoaringContainerView view) {
   return {view.begin(), RoaringContainerView::end()};
}

std::vector<uint16_t> allRows(uint32_t num_rows) {
   std::vector<uint16_t> rows;
   rows.reserve(num_rows);
   for (uint32_t row = 0; row < num_rows; ++row) {
      rows.push_back(static_cast<uint16_t>(row));
   }
   return rows;
}

/// The texts of `rows` of `chunk`, reconstructed in one call
std::vector<std::string> reconstruct(
   const SequenceChunk& chunk,
   const std::vector<uint16_t>& rows
) {
   std::vector<std::string> texts(rows.size(), std::string(chunk.sequenceLength(), '?'));
   std::vector<char*> pointers;
   pointers.reserve(texts.size());
   for (auto& text : texts) {
      pointers.push_back(text.data());
   }
   const auto container = containerOf(rows);
   reconstructSequences(chunk, RoaringContainerView{container}, pointers);
   return texts;
}

std::vector<std::string> reconstructAll(const SequenceChunk& chunk) {
   return reconstruct(chunk, allRows(chunk.numRows()));
}

/// Random rows over a random reference: each row is a run of the text at an offset, with
/// mutations (including gaps and ambiguous symbols) and runs of missing symbols, or null. The
/// expected texts carry the missing symbol wherever a row does not cover a position.
struct RandomRows {
   std::string reference;
   /// nullopt for a null row
   std::vector<std::optional<std::string>> texts;
   std::shared_ptr<const SequenceReference> shared_reference;
   SequenceChunk chunk;

   static RandomRows nucleotides(uint32_t seed, uint32_t num_rows, uint32_t length) {
      std::mt19937 random{seed};  // NOLINT(bugprone-random-generator-seed): reproducible
      const std::string bases = "ACGT";
      const std::string mutation_symbols = "ACGT-RY";
      std::string reference;
      for (uint32_t position = 0; position < length; ++position) {
         reference.push_back(bases[random() % bases.size()]);
      }
      auto shared_reference = SequenceReference::create<Nucleotide>(reference);
      SequenceChunkBuilder builder{shared_reference};
      std::vector<std::optional<std::string>> texts;
      for (uint32_t row = 0; row < num_rows; ++row) {
         if (random() % 13 == 0) {
            builder.addNullRow();
            texts.emplace_back(std::nullopt);
            continue;
         }
         const uint32_t start = random() % 3 == 0 ? random() % (length / 4) : 0;
         const uint32_t end = random() % 3 == 0 ? length - (random() % (length / 4)) : length;
         std::string text = reference;
         for (uint32_t position = 0; position < length; ++position) {
            if (random() % 10 == 0) {
               text[position] = mutation_symbols[random() % mutation_symbols.size()];
            }
         }
         if (random() % 4 == 0) {
            const uint32_t begin = random() % length;
            const uint32_t run_end =
               std::min(length, begin + 1 + static_cast<uint32_t>(random() % 20));
            for (uint32_t position = begin; position < run_end; ++position) {
               text[position] = 'N';
            }
         }
         builder.addText(std::string_view{text}.substr(start, end - start), start);
         for (uint32_t position = 0; position < length; ++position) {
            if (position < start || position >= end) {
               text[position] = 'N';
            }
         }
         texts.emplace_back(std::move(text));
      }
      auto chunk = std::move(builder).finish();
      return RandomRows{
         .reference = std::move(reference),
         .texts = std::move(texts),
         .shared_reference = std::move(shared_reference),
         .chunk = std::move(chunk),
      };
   }

   [[nodiscard]] std::string expectedText(uint16_t row) const {
      return texts[row].value_or(std::string(reference.size(), 'N'));
   }

   [[nodiscard]] std::vector<std::string> expectedTexts(const std::vector<uint16_t>& rows) const {
      std::vector<std::string> result;
      result.reserve(rows.size());
      for (const uint16_t row : rows) {
         result.push_back(expectedText(row));
      }
      return result;
   }
};

/// Every 3rd row plus a few at the ends: a selection in which some rows are skipped
std::vector<uint16_t> someRows(uint32_t num_rows) {
   std::vector<uint16_t> rows;
   rows.reserve(num_rows);
   for (uint32_t row = 0; row < num_rows; ++row) {
      if (row % 3 == 0 || row + 2 >= num_rows) {
         rows.push_back(static_cast<uint16_t>(row));
      }
   }
   return rows;
}

}  // namespace

TEST(SequenceReference, mapsSymbolsAndRejectsIllegalCharacters) {
   const auto reference = SequenceReference::create<Nucleotide>("AC-N");
   EXPECT_EQ(reference->length(), 4);
   EXPECT_EQ(reference->symbolAt(1), static_cast<uint8_t>(Nucleotide::Symbol::C));
   EXPECT_EQ(reference->symbolChar(static_cast<uint8_t>(Nucleotide::Symbol::GAP)), '-');
   EXPECT_EQ(reference->missingChar(), 'N');
   EXPECT_EQ(reference->numSymbols(), Nucleotide::COUNT);
   EXPECT_EQ(SequenceReference::create<AminoAcid>("MA")->missingChar(), 'X');
   EXPECT_THROW((void)SequenceReference::create<Nucleotide>("ACJ"), std::invalid_argument);

   const auto replaced = reference->withSymbolsReplaced(
      std::vector<std::pair<uint32_t, uint8_t>>{
         {0, static_cast<uint8_t>(Nucleotide::Symbol::T)},
      }
   );
   EXPECT_EQ(replaced->text(), "TC-N");
   EXPECT_EQ(reference->text(), "AC-N");
}

TEST(SequenceChunk, reconstructsTheTextsOfAllRows) {
   const auto rows = RandomRows::nucleotides(1, 300, 120);
   ASSERT_EQ(rows.chunk.numRows(), 300);
   EXPECT_EQ(reconstructAll(rows.chunk), rows.expectedTexts(allRows(300)));
}

TEST(SequenceChunk, reconstructsTheTextsOfSelectedRows) {
   const auto rows = RandomRows::nucleotides(2, 300, 120);
   const auto selected = someRows(300);
   EXPECT_EQ(reconstruct(rows.chunk, selected), rows.expectedTexts(selected));
   EXPECT_EQ(reconstruct(rows.chunk, {}), std::vector<std::string>{});
}

TEST(SequenceChunk, reconstructsEveryNucleotideAndAminoAcidSymbol) {
   {
      SequenceChunkBuilder builder{SequenceReference::create<Nucleotide>("A")};
      std::vector<std::string> expected;
      for (const auto symbol : Nucleotide::SYMBOLS) {
         const std::string text(1, Nucleotide::symbolToChar(symbol));
         builder.addText(text);
         expected.push_back(text);
      }
      EXPECT_EQ(reconstructAll(std::move(builder).finish()), expected);
   }
   {
      SequenceChunkBuilder builder{SequenceReference::create<AminoAcid>("A")};
      std::vector<std::string> expected;
      for (const auto symbol : AminoAcid::SYMBOLS) {
         const std::string text(1, AminoAcid::symbolToChar(symbol));
         builder.addText(text);
         expected.push_back(text);
      }
      EXPECT_EQ(reconstructAll(std::move(builder).finish()), expected);
   }
}

TEST(SequenceChunk, builderRejectsSequencesThatDoNotFit) {
   SequenceChunkBuilder builder{SequenceReference::create<Nucleotide>("ACGT")};
   EXPECT_THROW(builder.addText("ACGTA"), std::invalid_argument);
   EXPECT_THROW(builder.addText("ACG", 2), std::invalid_argument);
   EXPECT_THROW(builder.addText("AJ"), std::invalid_argument);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, symbolsAtEqualTheReconstructedCharacters) {
   const auto rows = RandomRows::nucleotides(3, 400, 90);
   for (const auto& selected : {allRows(400), someRows(400)}) {
      const auto container = containerOf(selected);
      std::vector<char> symbols(selected.size());
      for (uint32_t position = 0; position < 90; ++position) {
         symbolsAt(rows.chunk, position, RoaringContainerView{container}, symbols);
         for (size_t index = 0; index < selected.size(); ++index) {
            ASSERT_EQ(symbols[index], rows.expectedText(selected[index])[position])
               << "row " << selected[index] << " position " << position;
         }
      }
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, coveredRowsAndRowsWithSymbolsMatchTheTexts) {
   const auto rows = RandomRows::nucleotides(4, 500, 80);
   const auto& reference = *rows.shared_reference;
   for (const auto& selected : {allRows(500), someRows(500), std::vector<uint16_t>{7, 99}}) {
      const auto container = containerOf(selected);
      const RoaringContainerView selection{container};
      for (uint32_t position = 0; position < 80; ++position) {
         std::vector<uint16_t> expected_covered;
         for (const uint16_t row : selected) {
            if (rows.texts[row].has_value() && rows.expectedText(row)[position] != 'N') {
               expected_covered.push_back(row);
            }
         }
         ASSERT_EQ(
            valuesOf(RoaringContainerView{coveredRows(rows.chunk, position, selection)}),
            expected_covered
         );

         for (const std::string symbols : {"A", "N", "AN", "C-", "-NRY"}) {
            SymbolSet set;
            for (const char symbol : symbols) {
               set.add(static_cast<uint8_t>(Nucleotide::charToSymbol(symbol).value()));
            }
            std::vector<uint16_t> expected;
            for (const uint16_t row : selected) {
               if (
                  rows.texts[row].has_value() && symbols.contains(rows.expectedText(row)[position])
               ) {
                  expected.push_back(row);
               }
            }
            ASSERT_EQ(
               valuesOf(
                  RoaringContainerView{rowsWithSymbolsAt(rows.chunk, position, set, selection)}
               ),
               expected
            ) << "symbols "
              << symbols << " position " << position;
         }
      }
   }
   (void)reference;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, storedSymbolsAreTheRowsDifferingFromTheReference) {
   const auto rows = RandomRows::nucleotides(5, 200, 50);
   for (uint32_t position = 0; position < 50; ++position) {
      std::vector<std::vector<uint16_t>> expected(Nucleotide::COUNT);
      for (uint16_t row = 0; row < 200; ++row) {
         const char symbol = rows.expectedText(row)[position];
         if (symbol != 'N' && symbol != rows.reference[position]) {
            expected[static_cast<uint8_t>(Nucleotide::charToSymbol(symbol).value())].push_back(row);
         }
      }
      std::vector<std::vector<uint16_t>> actual(Nucleotide::COUNT);
      for (const auto& [symbol, stored] : storedSymbolsAt(rows.chunk, position)) {
         actual[symbol] = valuesOf(stored);
      }
      ASSERT_EQ(actual, expected) << "position " << position;
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, symbolCountsCountTheCoveredRows) {
   const auto rows = RandomRows::nucleotides(6, 300, 60);
   for (const auto& selected : {allRows(300), someRows(300)}) {
      SymbolCounts expected{static_cast<uint8_t>(Nucleotide::COUNT), 60};
      for (const uint16_t row : selected) {
         const std::string text = rows.expectedText(row);
         for (uint32_t position = 0; position < 60; ++position) {
            if (text[position] != 'N') {
               ++expected.at(
                  static_cast<uint8_t>(Nucleotide::charToSymbol(text[position]).value()), position
               );
            }
         }
      }
      SymbolCounts actual{static_cast<uint8_t>(Nucleotide::COUNT), 60};
      const auto container = containerOf(selected);
      addSymbolCounts(rows.chunk, RoaringContainerView{container}, actual);
      for (uint8_t symbol = 0; symbol < Nucleotide::COUNT; ++symbol) {
         for (uint32_t position = 0; position < 60; ++position) {
            ASSERT_EQ(actual.at(symbol, position), expected.at(symbol, position))
               << "symbol " << int{symbol} << " position " << position;
         }
      }
   }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, symbolGroupsPartitionTheSelectedRows) {
   const auto rows = RandomRows::nucleotides(7, 300, 70);
   for (const auto& selected : {allRows(300), someRows(300)}) {
      const auto container = containerOf(selected);
      const RoaringContainerView selection{container};
      for (uint32_t position = 0; position < 70; ++position) {
         const auto groups = groupRowsBySymbolAt(rows.chunk, position, selection);
         std::vector<int> group_of_row(300, -1);
         if (groups.every_row_symbol.has_value()) {
            for (const uint16_t row : selected) {
               group_of_row[row] = *groups.every_row_symbol;
            }
         }
         for (const auto& [symbol, group_rows] : groups.groups) {
            for (const uint16_t row : group_rows) {
               if (selection.contains(row)) {
                  ASSERT_EQ(group_of_row[row], -1) << "row " << row << " in two groups";
                  group_of_row[row] = symbol;
               }
            }
         }
         if (groups.null_rows.has_value()) {
            for (const uint16_t row : *groups.null_rows) {
               if (selection.contains(row)) {
                  group_of_row[row] = Nucleotide::COUNT;
               }
            }
         }
         for (const uint16_t row : selected) {
            const int expected =
               rows.texts[row].has_value()
                  ? static_cast<int>(
                       Nucleotide::charToSymbol(rows.expectedText(row)[position]).value()
                    )
                  : static_cast<int>(Nucleotide::COUNT);
            ASSERT_EQ(group_of_row[row], expected) << "row " << row << " position " << position;
         }
      }
   }
}

TEST(SequenceChunk, symbolGroupsCollapseAChunkWhoseRowsAllCarryTheSameSymbol) {
   SequenceChunkBuilder builder{SequenceReference::create<Nucleotide>("ACGT")};
   builder.addText("ACGT");
   builder.addText("ACG", 0);
   builder.addText("ACGT");
   const auto chunk = std::move(builder).finish();
   const auto all = containerOf(allRows(3));
   EXPECT_EQ(
      groupRowsBySymbolAt(chunk, 1, RoaringContainerView{all}).every_row_symbol,
      static_cast<uint8_t>(Nucleotide::Symbol::C)
   );
   EXPECT_FALSE(groupRowsBySymbolAt(chunk, 3, RoaringContainerView{all}).every_row_symbol);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, rebasingOntoAnotherReferenceKeepsTheTexts) {
   const auto rows = RandomRows::nucleotides(8, 300, 100);
   std::string other_text = rows.reference;
   std::mt19937 random{8};  // NOLINT(bugprone-random-generator-seed): reproducible
   for (uint32_t position = 0; position < 100; position += 1 + (random() % 4)) {
      other_text[position] = "ACGT-"[random() % 5];
   }
   const auto other = SequenceReference::create<Nucleotide>(other_text);
   const auto rebased = rows.chunk.rebasedOnto(other);
   EXPECT_EQ(rebased.sharedReference(), other);
   EXPECT_EQ(reconstructAll(rebased), rows.expectedTexts(allRows(300)));
   // Nothing is stored where a row carries the reference symbol
   for (uint32_t position = 0; position < 100; ++position) {
      for (const auto& [symbol, stored] : storedSymbolsAt(rebased, position)) {
         EXPECT_NE(symbol, other->symbolAt(position));
      }
   }
   // Rebasing back yields the original vertical index
   const auto back = rebased.rebasedOnto(rows.shared_reference);
   EXPECT_EQ(reconstructAll(back), rows.expectedTexts(allRows(300)));
   EXPECT_EQ(back.mutations().entries().size(), rows.chunk.mutations().entries().size());
   // Rebasing onto the same reference shares everything
   EXPECT_EQ(
      rows.chunk.rebasedOnto(rows.shared_reference).mutations().data().data(),
      rows.chunk.mutations().data().data()
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, compactionRemovesTheDeletedRows) {
   const auto rows = RandomRows::nucleotides(9, 200, 60);
   const auto layout = column::RowLayout::of(200);
   roaring::Roaring deleted_rows;
   for (uint32_t row = 0; row < 200; row += 4) {
      deleted_rows.add(row);
   }
   deleted_rows.add(199);
   const column::RowCompaction compaction{layout, Bitmap{&deleted_rows}};
   const auto compacted = rows.chunk.compacted(compaction, 0);

   std::vector<uint16_t> remaining;
   for (uint16_t row = 0; row < 200; ++row) {
      if (!deleted_rows.contains(row)) {
         remaining.push_back(row);
      }
   }
   ASSERT_EQ(compacted.numRows(), remaining.size());
   EXPECT_EQ(reconstructAll(compacted), rows.expectedTexts(remaining));
   for (size_t new_row = 0; new_row < remaining.size(); ++new_row) {
      EXPECT_EQ(
         compacted.isNull(static_cast<uint16_t>(new_row)),
         !rows.texts[remaining[new_row]].has_value()
      );
   }
   // The buffers of the original chunk are left as they were
   EXPECT_EQ(reconstructAll(rows.chunk), rows.expectedTexts(allRows(200)));
}

TEST(SequenceChunk, copiesShareTheBuffers) {
   const auto rows = RandomRows::nucleotides(10, 50, 30);
   const SequenceChunk copy = rows.chunk;  // NOLINT(performance-unnecessary-copy-initialization)
   EXPECT_EQ(copy.mutations().data().data(), rows.chunk.mutations().data().data());
   EXPECT_EQ(copy.coverage().data().data(), rows.chunk.coverage().data().data());
   EXPECT_EQ(copy.nullRows().payload().data(), rows.chunk.nullRows().payload().data());
   EXPECT_EQ(copy.sharedReference(), rows.chunk.sharedReference());
}

TEST(SequenceChunk, serializationRoundTripsTheBuffers) {
   const auto rows = RandomRows::nucleotides(11, 300, 80);
   std::stringstream buffer;
   {
      boost::archive::binary_oarchive output_archive(buffer);
      rows.chunk.save(output_archive);
   }
   boost::archive::binary_iarchive input_archive(buffer);
   const auto loaded = SequenceChunk::load(input_archive, rows.shared_reference);
   EXPECT_TRUE(std::ranges::equal(loaded.mutations().data(), rows.chunk.mutations().data()));
   EXPECT_TRUE(std::ranges::equal(loaded.coverage().data(), rows.chunk.coverage().data()));
   EXPECT_EQ(valuesOf(loaded.nullRows()), valuesOf(rows.chunk.nullRows()));
   EXPECT_EQ(reconstructAll(loaded), rows.expectedTexts(allRows(300)));
}

TEST(SequenceChunk, rejectsPartsThatDoNotFitTogether) {
   const auto rows = RandomRows::nucleotides(12, 50, 40);
   // A shorter reference than the stored positions
   EXPECT_THROW(
      rows.chunk.withMutations(
         SequenceReference::create<Nucleotide>("ACGT"), rows.chunk.mutations()
      ),
      std::runtime_error
   );
   // A null row that covers positions
   HorizontalCoverageIndexBuilder coverage;
   coverage.addRow(0, 4, std::span<const uint32_t>{});
   EXPECT_THROW(
      (SequenceChunk{
         SequenceReference::create<Nucleotide>("ACGT"),
         VerticalSequenceIndex{},
         std::move(coverage).finish(),
         std::make_shared<const RoaringContainer>(containerOf({0})),
      }),
      std::runtime_error
   );
}

}  // namespace rhydb::storage::sequence
