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
#include "rhydb/common/buffer.h"
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

/// Random rows over a random reference: each row is a run of a random variant of the reference (the
/// same for all rows, so the rows' most common symbols differ from the reference) at an offset,
/// with mutations (including gaps and ambiguous symbols) and runs of missing symbols, or null. The
/// expected texts carry the missing symbol wherever a row does not cover a position. With
/// `local_reference`, the chunk is stored against its own local reference
/// (`finishWithLocalReference`), otherwise against the random reference.
struct RandomRows {
   std::string reference;
   /// nullopt for a null row
   std::vector<std::optional<std::string>> texts;
   std::shared_ptr<const SequenceReference> shared_reference;
   SequenceChunk chunk;

   // NOLINTNEXTLINE(readability-function-cognitive-complexity)
   static RandomRows nucleotides(
      uint32_t seed,
      uint32_t num_rows,
      uint32_t length,
      bool local_reference = false
   ) {
      std::mt19937 random{seed};  // NOLINT(bugprone-random-generator-seed): reproducible
      const std::string bases = "ACGT";
      const std::string mutation_symbols = "ACGT-RY";
      std::string reference;
      for (uint32_t position = 0; position < length; ++position) {
         reference.push_back(bases[random() % bases.size()]);
      }
      std::string variant = reference;
      for (uint32_t position = 0; position < length; ++position) {
         if (random() % 5 == 0) {
            variant[position] = "ACGT-"[random() % 5];
         }
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
         std::string text = random() % 4 == 0 ? reference : variant;
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
      auto chunk = local_reference ? std::move(builder).finishWithLocalReference({})
                                   : std::move(builder).finish();
      if (local_reference) {
         shared_reference = chunk.sharedReference();
         reference = shared_reference->text();
      }
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceReference, storesOnlyTheDifferencesFromItsBase) {
   using Symbol = Nucleotide::Symbol;
   const auto base = SequenceReference::create<Nucleotide>("ACGTACGT");
   const auto symbol = [](Symbol value) { return static_cast<uint8_t>(value); };
   const auto derived = base->withSymbolsReplaced(
      std::vector<SequenceReference::SymbolAt>{{1, symbol(Symbol::T)}, {6, symbol(Symbol::A)}}
   );
   EXPECT_EQ(derived->text(), "ATGTACAT");
   EXPECT_EQ(derived->symbolAt(1), symbol(Symbol::T));
   EXPECT_EQ(derived->symbolAt(2), symbol(Symbol::G));
   EXPECT_EQ(derived->symbolAt(6), symbol(Symbol::A));
   EXPECT_EQ(
      derived->differencesFrom(*base),
      (std::vector<SequenceReference::SymbolAt>{{1, symbol(Symbol::T)}, {6, symbol(Symbol::A)}})
   );
   EXPECT_EQ(
      base->differencesFrom(*derived),
      (std::vector<SequenceReference::SymbolAt>{{1, symbol(Symbol::C)}, {6, symbol(Symbol::G)}})
   );
   EXPECT_LE(derived->differencesSizeInBytes(), 2 * sizeof(SequenceReference::SymbolAt));
   std::string written(8, '?');
   derived->writeText(written.data());
   EXPECT_EQ(written, "ATGTACAT");

   // Replacing a derived reference's symbols merges them into its differences, and a position
   // that carries the base symbol again is no difference anymore
   const auto twice = derived->withSymbolsReplaced(
      std::vector<SequenceReference::SymbolAt>{{0, symbol(Symbol::GAP)}, {1, symbol(Symbol::C)}}
   );
   EXPECT_EQ(twice->text(), "-CGTACAT");
   EXPECT_EQ(
      twice->differencesFrom(*base),
      (std::vector<SequenceReference::SymbolAt>{{0, symbol(Symbol::GAP)}, {6, symbol(Symbol::A)}})
   );

   // Equality compares the symbols, whatever the base
   EXPECT_EQ(*derived, *SequenceReference::create<Nucleotide>("ATGTACAT"));
   EXPECT_FALSE(*derived == *base);
   EXPECT_EQ(*base->withSymbolsReplaced({}), *base);
}

TEST(SequenceChunk, reconstructsTheTextsOfAllOrSelectedRows) {
   for (const bool local_reference : {false, true}) {
      const auto rows = RandomRows::nucleotides(1, 300, 120, local_reference);
      ASSERT_EQ(rows.chunk.numRows(), 300);
      for (const auto& selected : {allRows(300), someRows(300), std::vector<uint16_t>{}}) {
         EXPECT_EQ(reconstruct(rows.chunk, selected), rows.expectedTexts(selected));
      }
   }
}

TEST(SequenceChunk, reconstructsEveryNucleotideAndAminoAcidSymbol) {
   auto expect_every_symbol = []<typename SymbolType> {
      SequenceChunkBuilder builder{SequenceReference::create<SymbolType>("A")};
      std::vector<std::string> expected;
      for (const auto symbol : SymbolType::SYMBOLS) {
         expected.emplace_back(1, SymbolType::symbolToChar(symbol));
         builder.addText(expected.back());
      }
      EXPECT_EQ(reconstructAll(std::move(builder).finish()), expected);
   };
   expect_every_symbol.template operator()<Nucleotide>();
   expect_every_symbol.template operator()<AminoAcid>();
}

TEST(SequenceChunk, builderRejectsSequencesThatDoNotFit) {
   SequenceChunkBuilder builder{SequenceReference::create<Nucleotide>("ACGT")};
   EXPECT_THROW(builder.addText("ACGTA"), std::invalid_argument);
   EXPECT_THROW(builder.addText("ACG", 2), std::invalid_argument);
   EXPECT_THROW(builder.addText("AJ"), std::invalid_argument);
}

// Every per-position algorithm, at every position and for several selections, against the texts
// the rows were built from.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, perPositionAlgorithmsMatchTheTexts) {
   constexpr uint32_t NUM_ROWS = 500;
   constexpr uint32_t LENGTH = 80;
   for (const bool local_reference : {false, true}) {
      const auto rows = RandomRows::nucleotides(4, NUM_ROWS, LENGTH, local_reference);
      auto symbol_of = [](char character) {
         return static_cast<uint8_t>(Nucleotide::charToSymbol(character).value());
      };
      auto is_covered = [&](uint16_t row, uint32_t position) {
         return rows.texts[row].has_value() && rows.expectedText(row)[position] != 'N';
      };

      // The stored symbols are the rows differing from the reference.
      for (uint32_t position = 0; position < LENGTH; ++position) {
         std::vector<std::vector<uint16_t>> expected(Nucleotide::COUNT);
         for (uint16_t row = 0; row < NUM_ROWS; ++row) {
            const char symbol = rows.expectedText(row)[position];
            if (is_covered(row, position) && symbol != rows.reference[position]) {
               expected[symbol_of(symbol)].push_back(row);
            }
         }
         std::vector<std::vector<uint16_t>> actual(Nucleotide::COUNT);
         for (const auto& [symbol, stored] : storedSymbolsAt(rows.chunk, position)) {
            actual[symbol] = valuesOf(stored);
         }
         ASSERT_EQ(actual, expected) << "position " << position;
      }

      for (const auto& selected :
           {allRows(NUM_ROWS), someRows(NUM_ROWS), std::vector<uint16_t>{7, 99}}) {
         const auto container = containerOf(selected);
         const RoaringContainerView selection{container};
         SymbolCounts expected_counts{static_cast<uint8_t>(Nucleotide::COUNT), LENGTH};
         for (uint32_t position = 0; position < LENGTH; ++position) {
            SCOPED_TRACE(::testing::Message() << selected.size() << " rows at " << position);
            std::vector<char> symbols(selected.size());
            symbolsAt(rows.chunk, position, selection, symbols);
            std::vector<uint16_t> expected_covered;
            for (size_t index = 0; index < selected.size(); ++index) {
               const uint16_t row = selected[index];
               const char symbol = rows.expectedText(row)[position];
               ASSERT_EQ(symbols[index], symbol) << "row " << row;
               if (is_covered(row, position)) {
                  expected_covered.push_back(row);
                  ++expected_counts.at(symbol_of(symbol), position);
               }
            }
            ASSERT_EQ(
               valuesOf(RoaringContainerView{coveredRows(rows.chunk, position, selection)}),
               expected_covered
            );

            for (const std::string symbol_chars : {"A", "N", "AN", "C-", "-NRY"}) {
               SymbolSet set;
               std::vector<uint16_t> expected;
               for (const char symbol : symbol_chars) {
                  set.add(symbol_of(symbol));
               }
               for (const uint16_t row : selected) {
                  if (
                     rows.texts[row].has_value() &&
                     symbol_chars.contains(rows.expectedText(row)[position])
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
                 << symbol_chars;
            }

            // The symbol groups partition the selected rows; null rows form a group of their own.
            const auto groups = groupRowsBySymbolAt(rows.chunk, position, selection);
            std::vector<int> group_of_row(NUM_ROWS, -1);
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
               ASSERT_EQ(
                  group_of_row[row],
                  rows.texts[row].has_value() ? symbol_of(rows.expectedText(row)[position])
                                              : Nucleotide::COUNT
               ) << "row "
                 << row;
            }
         }

         SymbolCounts counts{static_cast<uint8_t>(Nucleotide::COUNT), LENGTH};
         addSymbolCounts(rows.chunk, selection, counts);
         for (uint8_t symbol = 0; symbol < Nucleotide::COUNT; ++symbol) {
            for (uint32_t position = 0; position < LENGTH; ++position) {
               ASSERT_EQ(counts.at(symbol, position), expected_counts.at(symbol, position))
                  << "symbol " << int{symbol} << " position " << position;
            }
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
      rows.chunk.rebasedOnto(rows.shared_reference).mutationsBuffer(), rows.chunk.mutationsBuffer()
   );
   // Rebasing shares the coverage
   EXPECT_EQ(rebased.coverageBuffer(), rows.chunk.coverageBuffer());
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

TEST(SequenceChunk, copiesShareTheBuffersAndKeepThemAlive) {
   std::optional<RandomRows> rows = RandomRows::nucleotides(10, 50, 30);
   const SequenceChunk copy = rows->chunk;
   EXPECT_EQ(copy.mutationsBuffer(), rows->chunk.mutationsBuffer());
   EXPECT_EQ(copy.coverageBuffer(), rows->chunk.coverageBuffer());
   EXPECT_EQ(copy.mutations().entries().data(), rows->chunk.mutations().entries().data());
   EXPECT_EQ(copy.coverage().starts().data(), rows->chunk.coverage().starts().data());
   EXPECT_EQ(copy.nullRows().payload().data(), rows->chunk.nullRows().payload().data());
   EXPECT_EQ(copy.sharedReference(), rows->chunk.sharedReference());

   const auto expected = rows->expectedTexts(allRows(50));
   rows.reset();
   EXPECT_EQ(copy.mutationsBuffer().use_count(), 1);
   EXPECT_EQ(reconstructAll(copy), expected);
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
   EXPECT_TRUE(
      std::ranges::equal(loaded.mutationsBuffer()->bytes(), rows.chunk.mutationsBuffer()->bytes())
   );
   EXPECT_TRUE(
      std::ranges::equal(loaded.coverageBuffer()->bytes(), rows.chunk.coverageBuffer()->bytes())
   );
   EXPECT_EQ(valuesOf(loaded.nullRows()), valuesOf(rows.chunk.nullRows()));
   EXPECT_EQ(reconstructAll(loaded), rows.expectedTexts(allRows(300)));
}

// Validation runs on `load`, and in debug builds already on construction.
TEST(SequenceChunk, rejectsPartsThatDoNotFitTogether) {
   const auto rows = RandomRows::nucleotides(12, 50, 40);
   // A shorter reference than the stored positions
   EXPECT_THROW(
      rows.chunk
         .withMutations(SequenceReference::create<Nucleotide>("ACGT"), rows.chunk.mutationsBuffer())
         .validate(),
      std::runtime_error
   );
   // A null row that covers positions
   HorizontalCoverageIndexBuilder coverage;
   coverage.addRow(0, 4, std::span<const uint32_t>{});
   EXPECT_THROW(
      (SequenceChunk{
         SequenceReference::create<Nucleotide>("ACGT"),
         std::make_shared<const common::Buffer>(VerticalSequenceIndexBuilder{}.finish()),
         std::make_shared<const common::Buffer>(std::move(coverage).finish()),
         std::make_shared<const RoaringContainer>(containerOf({0})),
      }
          .validate()),
      std::runtime_error
   );
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SequenceChunk, theLocalReferenceHasTheMostCommonCoveredSymbolAtEveryPosition) {
   const auto original = RandomRows::nucleotides(13, 400, 90);
   const auto rows = RandomRows::nucleotides(13, 400, 90, /*local_reference=*/true);
   EXPECT_NE(rows.reference, original.reference);
   for (uint32_t position = 0; position < 90; ++position) {
      std::vector<uint32_t> counts(Nucleotide::COUNT, 0);
      for (uint16_t row = 0; row < 400; ++row) {
         const char symbol = rows.expectedText(row)[position];
         if (symbol != 'N') {
            ++counts[static_cast<uint8_t>(Nucleotide::charToSymbol(symbol).value())];
         }
      }
      const auto original_symbol =
         static_cast<uint8_t>(Nucleotide::charToSymbol(original.reference[position]).value());
      // The first symbol with the highest count, unless the original reference symbol has it
      uint8_t expected = original_symbol;
      for (uint8_t symbol = 0; symbol < Nucleotide::COUNT; ++symbol) {
         if (counts[symbol] > counts[expected]) {
            expected = symbol;
         }
      }
      ASSERT_EQ(rows.shared_reference->symbolAt(position), expected) << "position " << position;
   }
   // Choosing again changes nothing
   EXPECT_TRUE(mostCommonSymbols(rows.chunk).empty());
}

TEST(SequenceChunk, finishingWithALocalReferenceSharesAKnownEqualReference) {
   auto make_builder = [] {
      SequenceChunkBuilder builder{SequenceReference::create<Nucleotide>("ACGT")};
      builder.addText("ACTT");
      builder.addText("ACTT");
      builder.addText("ACGT");
      return builder;
   };
   const auto known = SequenceReference::create<Nucleotide>("ACTT");
   const auto other = SequenceReference::create<Nucleotide>("AAAA");
   const auto chunk = make_builder().finishWithLocalReference(
      std::vector<std::shared_ptr<const SequenceReference>>{other, known}
   );
   EXPECT_EQ(chunk.sharedReference(), known);
   EXPECT_EQ(reconstructAll(chunk), (std::vector<std::string>{"ACTT", "ACTT", "ACGT"}));

   const auto unknown = make_builder().finishWithLocalReference({});
   EXPECT_EQ(unknown.reference().text(), "ACTT");
   EXPECT_NE(unknown.sharedReference(), known);

   // A chunk that keeps the reference keeps the builder's reference object
   const auto reference = SequenceReference::create<Nucleotide>("ACGT");
   SequenceChunkBuilder builder{reference};
   builder.addText("ACGT");
   builder.addNullRow();
   EXPECT_EQ(std::move(builder).finishWithLocalReference({}).sharedReference(), reference);
}

}  // namespace rhydb::storage::sequence
