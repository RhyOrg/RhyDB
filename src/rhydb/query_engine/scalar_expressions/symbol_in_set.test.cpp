#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/database_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/storage/reference_genomes.h"

namespace rhydb::query_engine::scalar_expressions {
namespace {

using Symbol = Nucleotide::Symbol;

nlohmann::json row(const std::string& primary_key, const nlohmann::json& sequence) {
   nlohmann::json segment = nullptr;
   if (!sequence.is_null()) {
      segment = {{"sequence", sequence}, {"insertions", nlohmann::json::array()}};
   }
   return {
      {"primaryKey", primary_key},
      {"segment1", segment},
      {"unaligned_segment1", nullptr},
   };
}

// Rows: 0 = reference, 1 = not covered, 2 = mutated, 3 = no sequence
std::shared_ptr<Database> createDatabase() {
   auto database = std::make_shared<Database>();
   initialize::Initializer::createTableInDatabase(
      schema::TableName::getDefault(),
      config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
  primaryKey: "primaryKey"
)"),
      ReferenceGenomes{{{"segment1", "ATGC"}}, {}},
      {},
      common::PhyloTree(),
      /*without_unaligned_sequences=*/false,
      *database
   );
   std::stringstream ndjson;
   ndjson << row("reference", "ATGC").dump() << "\n"
          << row("not_covered", "NNNN").dump() << "\n"
          << row("mutated", "CATG").dump() << "\n"
          << row("no_sequence", nullptr).dump() << "\n";
   database->appendData(schema::TableName::getDefault(), ndjson);
   return database;
}

roaring::Roaring selectedRows(
   const Database& database,
   std::vector<Symbol> symbols,
   bool with_null_rows
) {
   const auto& table = *database.tables.at(schema::TableName::getDefault());
   const SymbolInSet<Nucleotide> expression(
      {.name = "segment1", .type = schema::DataType::nucleotideSequence(4)},
      /*position_idx=*/0,
      std::move(symbols)
   );
   return expression.compile(table, with_null_rows)->evaluate().toRoaring();
}

class SymbolInSetCompile : public ::testing::Test {
  protected:
   static std::shared_ptr<Database> database;

   static void SetUpTestSuite() { database = createDatabase(); }
   static void TearDownTestSuite() { database.reset(); }
};

std::shared_ptr<Database> SymbolInSetCompile::database = nullptr;

constexpr uint32_t NO_SEQUENCE_ROW = 3;

}  // namespace

TEST_F(SymbolInSetCompile, onlyMutationsSelectsTheNullSequencesExactlyIfRequested) {
   const std::vector<Symbol> symbols{Symbol::C};
   EXPECT_EQ(selectedRows(*database, symbols, false), roaring::Roaring({2}));
   EXPECT_EQ(selectedRows(*database, symbols, true), roaring::Roaring({2, NO_SEQUENCE_ROW}));
}

TEST_F(SymbolInSetCompile, withReferenceSelectsTheNullSequencesExactlyIfRequested) {
   const std::vector<Symbol> symbols{Symbol::A};
   EXPECT_EQ(selectedRows(*database, symbols, false), roaring::Roaring({0}));
   EXPECT_EQ(selectedRows(*database, symbols, true), roaring::Roaring({0, NO_SEQUENCE_ROW}));
}

TEST_F(SymbolInSetCompile, withMissingSymbolSelectsTheNullSequencesExactlyIfRequested) {
   const std::vector<Symbol> symbols{Symbol::N};
   EXPECT_EQ(selectedRows(*database, symbols, false), roaring::Roaring({1}));
   EXPECT_EQ(selectedRows(*database, symbols, true), roaring::Roaring({1, NO_SEQUENCE_ROW}));
}

TEST_F(SymbolInSetCompile, withMissingSymbolAndReferenceSelectsTheNullSequencesExactlyIfRequested) {
   const std::vector<Symbol> symbols{Symbol::A, Symbol::N};
   EXPECT_EQ(selectedRows(*database, symbols, false), roaring::Roaring({0, 1}));
   EXPECT_EQ(selectedRows(*database, symbols, true), roaring::Roaring({0, 1, NO_SEQUENCE_ROW}));
}

namespace {

// Two chunks (one per append) whose local references differ at position 0: most rows of the first
// carry T there, most rows of the second the reference symbol A.
const std::vector<std::vector<std::optional<std::string>>> CHUNKS_WITH_DIFFERENT_REFERENCES{
   {"TTGC", "TTGC", "ATGC", "NTGC", std::nullopt, "TTGC", "-TGC"},
   {"ATGC", "CTGC", "NTGC", std::nullopt, "-TGC", "ATGC", "TTGC"},
};

std::shared_ptr<Database> createDatabaseWithDifferentReferences() {
   auto database = std::make_shared<Database>();
   initialize::Initializer::createTableInDatabase(
      schema::TableName::getDefault(),
      config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: "dummy name"
  metadata:
    - name: "primaryKey"
      type: "string"
  primaryKey: "primaryKey"
)"),
      ReferenceGenomes{{{"segment1", "ATGC"}}, {}},
      {},
      common::PhyloTree(),
      /*without_unaligned_sequences=*/false,
      *database
   );
   for (size_t chunk_id = 0; chunk_id < CHUNKS_WITH_DIFFERENT_REFERENCES.size(); ++chunk_id) {
      std::stringstream ndjson;
      const auto& sequences = CHUNKS_WITH_DIFFERENT_REFERENCES[chunk_id];
      for (size_t row_in_chunk = 0; row_in_chunk < sequences.size(); ++row_in_chunk) {
         const auto& sequence = sequences[row_in_chunk];
         ndjson << row(
                      fmt::format("{}_{}", chunk_id, row_in_chunk),
                      sequence.has_value() ? nlohmann::json(*sequence) : nlohmann::json(nullptr)
                   )
                      .dump()
                << "\n";
      }
      database->appendData(schema::TableName::getDefault(), ndjson);
   }
   return database;
}

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SymbolInSetCompileWithLocalReferences, selectsTheRowsWithTheSymbolsInEveryChunk) {
   const auto database = createDatabaseWithDifferentReferences();
   const auto& table = *database->tables.at(schema::TableName::getDefault());
   const auto& column = table.getColumn<Nucleotide::Column>("segment1");
   ASSERT_EQ(column.numChunks(), 2);
   ASSERT_EQ(column.chunk(0).reference().text(), "TTGC");
   ASSERT_EQ(column.chunk(1).sharedReference(), column.reference());

   const std::vector<Symbol> candidates{
      Symbol::A,
      Symbol::C,
      Symbol::T,
      Symbol::GAP,
      Symbol::N,
      Symbol::G,
   };
   for (uint32_t subset = 0; subset < (1U << candidates.size()); ++subset) {
      std::vector<Symbol> symbols;
      std::string symbol_chars;
      for (size_t index = 0; index < candidates.size(); ++index) {
         if (((subset >> index) & 1U) != 0) {
            symbols.push_back(candidates[index]);
            symbol_chars.push_back(Nucleotide::symbolToChar(candidates[index]));
         }
      }
      for (const bool with_null_rows : {false, true}) {
         roaring::Roaring expected;
         roaring::Roaring all_rows;
         for (uint32_t chunk_id = 0; chunk_id < CHUNKS_WITH_DIFFERENT_REFERENCES.size();
              ++chunk_id) {
            const auto& sequences = CHUNKS_WITH_DIFFERENT_REFERENCES[chunk_id];
            for (uint32_t row_in_chunk = 0; row_in_chunk < sequences.size(); ++row_in_chunk) {
               const uint32_t row_id = (chunk_id << 16U) | row_in_chunk;
               all_rows.add(row_id);
               const auto& sequence = sequences[row_in_chunk];
               if (sequence.has_value() ? symbol_chars.contains(sequence->at(0)) : with_null_rows) {
                  expected.add(row_id);
               }
            }
         }
         const SymbolInSet<Nucleotide> expression(
            {.name = "segment1", .type = schema::DataType::nucleotideSequence(4)},
            /*position_idx=*/0,
            symbols
         );
         auto compiled = expression.compile(table, with_null_rows);
         EXPECT_EQ(compiled->evaluate().toRoaring(), expected)
            << "symbols {" << symbol_chars << "} with null rows " << with_null_rows << ": "
            << compiled->toString();
         const auto negated = filter::operators::Operator::negate(std::move(compiled));
         EXPECT_EQ(negated->evaluate().toRoaring(), all_rows - expected)
            << "negated symbols {" << symbol_chars << "} with null rows " << with_null_rows;
      }
   }
}

}  // namespace rhydb::query_engine::scalar_expressions
