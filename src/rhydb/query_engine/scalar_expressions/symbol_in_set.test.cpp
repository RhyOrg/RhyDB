#include "rhydb/query_engine/scalar_expressions/symbol_in_set.h"

#include <memory>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/config/database_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
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
      {},
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
      {.name = "segment1", .type = schema::DataType::NUCLEOTIDE_SEQUENCE},
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

}  // namespace rhydb::query_engine::scalar_expressions
