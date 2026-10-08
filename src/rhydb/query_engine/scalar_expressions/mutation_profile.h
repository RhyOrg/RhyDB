#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <type_traits>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/table.h"

namespace rhydb {
class Nucleotide;
}

namespace rhydb::query_engine::scalar_expressions {

template <typename SymbolType>
class MutationProfile : public ScalarExpression {
  public:
   struct Mutation {
      uint32_t position_idx;  // 0-indexed
      SymbolType::Symbol symbol;
   };

   struct QuerySequenceInput {
      std::string sequence;
   };

   struct SequenceIdInput {
      std::string id;
   };

   struct MutationsInput {
      std::vector<Mutation> mutations;
   };

   using ProfileInput = std::variant<QuerySequenceInput, SequenceIdInput, MutationsInput>;

  private:
   schema::ColumnIdentifier column;
   uint32_t distance;
   ProfileInput input;

   [[nodiscard]] std::vector<typename SymbolType::Symbol> buildProfileFromQuerySequence(
      const storage::column::SequenceColumn<SymbolType>& sequence_column
   ) const;

   [[nodiscard]] std::vector<typename SymbolType::Symbol> buildProfileFromSequenceId(
      const storage::Table& table,
      const std::string& valid_sequence_name
   ) const;

   [[nodiscard]] std::vector<typename SymbolType::Symbol> buildProfileFromMutations(
      const storage::column::SequenceColumn<SymbolType>& sequence_column
   ) const;

  public:
   explicit MutationProfile(schema::ColumnIdentifier column, uint32_t distance, ProfileInput input);

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<MutationProfile>(column, distance, input);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = std::is_same_v<SymbolType, Nucleotide>
                                   ? Kind::MUTATION_PROFILE_NUCLEOTIDE
                                   : Kind::MUTATION_PROFILE_AMINO_ACID;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
