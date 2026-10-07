#include "rhydb/query_engine/scalar_expressions/string_search.h"

#include <memory>
#include <utility>

#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/bitmap_producer.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"

namespace rhydb::query_engine::scalar_expressions {

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
template <typename GenericStringColumn>
std::unique_ptr<filter::operators::Operator> createMatchingBitmap(
   const GenericStringColumn& string_column,
   const std::shared_ptr<const RE2>& search_expression,
   storage::column::RowLayout row_layout
) {
   auto producer = [&string_column, search_expression, row_layout]() {
      roaring::Roaring result_bitmap;
      for (const auto row_id : row_layout) {
         const std::string full_string = string_column.getValueString(row_id);
         if (re2::RE2::PartialMatch(full_string, *search_expression)) {
            result_bitmap.add(row_id.toGlobal());
         }
      }
      return Bitmap(std::move(result_bitmap));
   };
   return std::make_unique<filter::operators::BitmapProducer>(
      std::move(producer), std::move(row_layout)
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

std::unique_ptr<filter::operators::Operator> StringSearch::compile(const storage::Table& table
) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.hasColumn<storage::column::StringColumn>(column.name) ||
         table.hasColumn<storage::column::DictionaryEncodedColumn>(column.name),
      "The column '{}' is not of type string",
      column.name
   );

   if (table.hasColumn<storage::column::DictionaryEncodedColumn>(column.name)) {
      const auto& string_column =
         table.getColumn<storage::column::DictionaryEncodedColumn>(column.name);
      return createMatchingBitmap(string_column, search_expression, table.row_layout);
   }
   RHYDB_ASSERT(table.hasColumn<storage::column::StringColumn>(column.name));
   const auto& string_column = table.getColumn<storage::column::StringColumn>(column.name);
   return createMatchingBitmap(string_column, search_expression, table.row_layout);
}

}  // namespace rhydb::query_engine::scalar_expressions
