#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

class LineageFilter : public ScalarExpression {
   schema::ColumnIdentifier column;
   std::optional<std::string> lineage;
   std::optional<rhydb::common::RecombinantEdgeFollowingMode> sublineage_mode;

  public:
   explicit LineageFilter(
      schema::ColumnIdentifier column,
      std::optional<std::string> lineage,
      std::optional<rhydb::common::RecombinantEdgeFollowingMode> sublineage_mode
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<LineageFilter>(column, lineage, sublineage_mode);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::LINEAGE_FILTER;
   [[nodiscard]] Kind kind() const override { return KIND; }

   [[nodiscard]] std::vector<schema::ColumnIdentifier> freeIUs() const override;

   [[nodiscard]] std::unique_ptr<ScalarExpression> rewrite(
      const storage::Table& table,
      AmbiguityMode mode
   ) const override;

   [[nodiscard]] std::unique_ptr<filter::operators::Operator> compile(
      const storage::Table& table
   ) const override;

  private:
   [[nodiscard]] std::optional<const roaring::Roaring*> getBitmapForValue(
      const rhydb::storage::column::DictionaryEncodedColumn& lineage_column
   ) const;
};

}  // namespace rhydb::query_engine::scalar_expressions
