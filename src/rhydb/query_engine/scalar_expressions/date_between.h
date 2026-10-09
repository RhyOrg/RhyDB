#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rhydb/common/date32.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/range_selection.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/date32_column.h"

namespace rhydb::query_engine::scalar_expressions {

class DateBetween : public ScalarExpression {
  private:
   schema::ColumnIdentifier column;
   std::optional<rhydb::common::Date32> date_from;
   std::optional<rhydb::common::Date32> date_to;

   [[nodiscard]] std::vector<rhydb::query_engine::filter::operators::RangeSelection::Range>
   computeRangesOfSortedColumn(const rhydb::storage::column::Date32Column& date_column) const;

  public:
   explicit DateBetween(
      schema::ColumnIdentifier column,
      std::optional<rhydb::common::Date32> date_from,
      std::optional<rhydb::common::Date32> date_to
   );

   [[nodiscard]] std::unique_ptr<ScalarExpression> clone() const override {
      return std::make_unique<DateBetween>(column, date_from, date_to);
   }

   [[nodiscard]] std::string toString() const override;
   static constexpr Kind KIND = Kind::DATE_BETWEEN;
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
