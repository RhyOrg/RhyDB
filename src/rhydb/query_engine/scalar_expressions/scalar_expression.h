#pragma once

#include <memory>
#include <string>
#include <vector>

#include <arrow/compute/expression.h>
#include <arrow/result.h>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::scalar_expressions {

class ScalarExpression {
  protected:
   ScalarExpression();

  public:
   virtual ~ScalarExpression() = default;

   /// UPPER_BOUND returns the upper bound of sequences matching this expression (i.e. ambiguous
   /// codes count as matches), LOWER_BOUND returns the lower bound of sequences matching this
   /// expression (i.e. ambiguous codes in negations count as matches)
   /// NONE does not specially consider ambiguous symbols
   enum AmbiguityMode : uint8_t { UPPER_BOUND, LOWER_BOUND, NONE };

   // For clean type-checks
   enum class Kind : uint8_t {
      AND,
      OR,
      AT,
      CONCAT,
      ISO_WEEK,
      N_OF,
      NEGATION,
      MAYBE,
      EXACT,
      COMPARISON,
      DATE_BETWEEN,
      FIELD_REF,
      FLOAT_BETWEEN,
      INT_BETWEEN,
      IS_NULL,
      LINEAGE_FILTER,
      PHYLO_CHILD_FILTER,
      STRING_IN_SET,
      STRING_SEARCH,
      INT32_LITERAL,
      INT64_LITERAL,
      FLOAT_LITERAL,
      STRING_LITERAL,
      BOOL_LITERAL,
      DATE_LITERAL,
      NULL_LITERAL,
      HAS_MUTATION_NUCLEOTIDE,
      HAS_MUTATION_AMINO_ACID,
      INSERTION_CONTAINS_NUCLEOTIDE,
      INSERTION_CONTAINS_AMINO_ACID,
      MUTATION_PROFILE_NUCLEOTIDE,
      MUTATION_PROFILE_AMINO_ACID,
      SYMBOL_EQUALS_NUCLEOTIDE,
      SYMBOL_EQUALS_AMINO_ACID,
      SYMBOL_IN_SET_NUCLEOTIDE,
      SYMBOL_IN_SET_AMINO_ACID,
      ZSTD_DECOMPRESS_SCALAR,
   };

   /// The column type this expression evaluates to. All current expressions are
   /// boolean filter predicates; non-boolean scalar expressions may override this.
   [[nodiscard]] virtual schema::DataType type() const { return schema::DataType::BOOL; }

   /// The concrete type of this expression, used by isA<>/dynCast<>.
   [[nodiscard]] virtual Kind kind() const = 0;

   [[nodiscard]] virtual std::string toString() const = 0;

   /// The columns ("identifiable units") this expression references and that an
   /// upstream node must therefore provide. Literals reference none; a column
   /// reference yields that column. Used by column narrowing to keep the child
   /// columns a scalar expression depends on alive.
   [[nodiscard]] virtual std::vector<schema::ColumnIdentifier> freeIUs() const { return {}; }

   /// Translates this expression into an Arrow compute expression.
   /// The default returns `NotImplemented`;
   [[nodiscard]] virtual arrow::Result<arrow::compute::Expression> toArrowExpression() const;

   [[nodiscard]] virtual std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const = 0;

   [[nodiscard]] virtual std::unique_ptr<ScalarExpression> clone() const = 0;

   [[nodiscard]] virtual std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const = 0;
};

ScalarExpression::AmbiguityMode invertMode(ScalarExpression::AmbiguityMode mode);

/// `To` must expose a `static constexpr Kind KIND` identifying its concrete type
template <typename To, typename From>
[[nodiscard]] bool isA(const From* expression) {
   return expression != nullptr && expression->kind() == To::KIND;
}

template <typename To, typename From>
[[nodiscard]] const To* dynCast(const From* expression) {
   return isA<To>(expression) ? static_cast<const To*>(expression) : nullptr;
}

template <typename To, typename From>
[[nodiscard]] To* dynCast(From* expression) {
   return isA<To>(expression) ? static_cast<To*>(expression) : nullptr;
}

template <typename T>
void appendVectorToVector(
   std::vector<std::unique_ptr<T>>& vec_1,
   std::vector<std::unique_ptr<T>>& vec_2
) {
   std::ranges::transform(vec_1, std::back_inserter(vec_2), [&](std::unique_ptr<T>& ele) {
      return std::move(ele);
   });
}

using ScalarExpressionVector = std::vector<std::unique_ptr<ScalarExpression>>;

}  // namespace rhydb::query_engine::scalar_expressions
