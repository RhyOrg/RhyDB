#pragma once

#include <memory>
#include <string>
#include <vector>

#include <re2/re2.h>
#include <nlohmann/json_fwd.hpp>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

class StringSearch : public ScalarExpression {
  private:
   schema::ColumnIdentifier column;
   std::shared_ptr<const re2::RE2> search_expression;

  public:
   explicit StringSearch(
      schema::ColumnIdentifier column,
      std::unique_ptr<re2::RE2> search_expression
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<StringSearch>(
         column, std::make_unique<re2::RE2>(search_expression->pattern())
      );
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::STRING_SEARCH;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;
};

}  // namespace rhydb::query_engine::scalar_expressions
