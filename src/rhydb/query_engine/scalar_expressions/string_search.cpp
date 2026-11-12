#include "rhydb/query_engine/scalar_expressions/string_search.h"

#include <memory>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

using filter::operators::Operator;
using filter::operators::Predicate;
using filter::operators::Selection;
using storage::column::DictionaryEncodedColumn;
using storage::column::RowId;
using storage::column::RowLayout;
using storage::column::StringColumn;

StringSearch::StringSearch(
   schema::ColumnIdentifier column,
   std::unique_ptr<re2::RE2> search_expression
)
    : column(std::move(column)),
      search_expression(std::move(search_expression)) {}

std::string StringSearch::toString() const {
   return fmt::format("column {} regex_matches \"{}\"", column.name, search_expression->pattern());
}

std::vector<schema::ColumnIdentifier> StringSearch::freeIUs() const {
   return {column};
}

namespace {

/// Matches the rows whose value contains a match of `search_expression` (or, if negated, does not)
template <typename GenericStringColumn>
class MatchesSearchExpression : public Predicate {
   const GenericStringColumn& string_column;
   std::shared_ptr<const RE2> search_expression;
   bool negated;
   /// Whether the rows where the column is null match
   bool with_nulls;

  public:
   MatchesSearchExpression(
      const GenericStringColumn& string_column,
      std::shared_ptr<const RE2> search_expression,
      bool negated,
      bool with_nulls
   )
       : string_column(string_column),
         search_expression(std::move(search_expression)),
         negated(negated),
         with_nulls(with_nulls) {}

   [[nodiscard]] std::string toString() const override {
      return fmt::format(
         "{}{} regex_matches \"{}\"",
         negated ? "!" : "",
         string_column.metadata->column_name,
         search_expression->pattern()
      );
   }

   [[nodiscard]] bool match(RowId row_id) const override {
      if (string_column.isNull(row_id)) {
         return with_nulls;
      }
      const std::string full_string = string_column.getValueString(row_id);
      return re2::RE2::PartialMatch(full_string, *search_expression) != negated;
   }

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override {
      return std::make_unique<MatchesSearchExpression>(
         string_column, search_expression, negated, with_nulls
      );
   }

   [[nodiscard]] std::unique_ptr<Predicate> negate() const override {
      return std::make_unique<MatchesSearchExpression>(
         string_column, search_expression, !negated, !with_nulls
      );
   }
};

template <typename GenericStringColumn>
std::unique_ptr<Operator> createMatchingSelection(
   const GenericStringColumn& string_column,
   const std::shared_ptr<const RE2>& search_expression,
   RowLayout row_layout,
   bool with_null_rows
) {
   return std::make_unique<Selection>(
      std::make_unique<MatchesSearchExpression<GenericStringColumn>>(
         string_column, search_expression, /*negated=*/false, with_null_rows
      ),
      std::move(row_layout)
   );
}

}  // namespace

std::unique_ptr<ScalarExpression> StringSearch::rewrite(
   const storage::Table& /*table*/,
   AmbiguityMode /*mode*/
) const {
   return std::make_unique<StringSearch>(
      column, std::make_unique<re2::RE2>(search_expression->pattern())
   );
}

std::unique_ptr<Operator> StringSearch::compile(
   const storage::Table& table,
   bool with_null_rows
) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.hasColumn<StringColumn>(column.name) ||
         table.hasColumn<DictionaryEncodedColumn<StringColumn>>(column.name),
      "The column '{}' is not of type string",
      column.name
   );

   if (table.hasColumn<DictionaryEncodedColumn<StringColumn>>(column.name)) {
      const auto& string_column =
         table.getColumn<DictionaryEncodedColumn<StringColumn>>(column.name);
      return createMatchingSelection(
         string_column, search_expression, table.row_layout, with_null_rows
      );
   }
   RHYDB_ASSERT(table.hasColumn<StringColumn>(column.name));
   const auto& string_column = table.getColumn<StringColumn>(column.name);
   return createMatchingSelection(
      string_column, search_expression, table.row_layout, with_null_rows
   );
}

}  // namespace rhydb::query_engine::scalar_expressions
