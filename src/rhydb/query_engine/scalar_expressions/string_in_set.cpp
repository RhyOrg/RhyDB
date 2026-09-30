#include "rhydb/query_engine/scalar_expressions/string_in_set.h"

#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/query_engine/filter/operators/string_in_set.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/scalar_expressions/comparison.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/query_engine/scalar_expressions/or.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/string_column.h"

namespace rhydb::query_engine::scalar_expressions {

using storage::column::StringColumn;

StringInSet::StringInSet(schema::ColumnIdentifier column, std::unordered_set<std::string> values)
    : column(std::move(column)),
      values(std::move(values)) {}

std::string StringInSet::toString() const {
   std::vector<std::string> sorted_values;
   std::ranges::copy(values, std::back_inserter(sorted_values));
   std::ranges::sort(sorted_values);
   return fmt::format("{} IN [{}]", column.name, fmt::join(sorted_values, ","));
}

std::vector<schema::ColumnIdentifier> StringInSet::freeIUs() const {
   return {column};
}

std::unique_ptr<ScalarExpression> StringInSet::rewrite(
   const storage::Table& table,
   AmbiguityMode /*mode*/
) const {
   CHECK_RHYDB_QUERY(
      table.schema->getColumn(column.name).has_value(),
      "The database does not contain the column '{}'",
      column.name
   );
   CHECK_RHYDB_QUERY(
      table.hasColumn<StringColumn>(column.name) ||
         table.hasColumn<storage::column::DictionaryEncodedColumn>(column.name),
      "The column '{}' is not of type string",
      column.name
   );

   // We do not change expressions for StringColumn
   if (table.hasColumn<StringColumn>(column.name)) {
      return std::make_unique<StringInSet>(column, values);
   }

   // We want to improve DictionaryEncodedColumn by using our Indexes directly -> one equality
   // comparison per value, each of which compiles to a single dictionary index lookup
   std::vector<std::unique_ptr<ScalarExpression>> string_equal_expressions;
   string_equal_expressions.reserve(values.size());
   for (const auto& value : values) {
      string_equal_expressions.emplace_back(std::make_unique<Comparison>(
         std::make_unique<FieldRef>(column),
         std::make_unique<StringLiteral>(value),
         filter::operators::Comparator::EQUALS
      ));
   }
   return std::make_unique<Or>(std::move(string_equal_expressions));
}

std::unique_ptr<filter::operators::Operator> StringInSet::compile(const storage::Table& table
) const {
   RHYDB_ASSERT(table.hasColumn<StringColumn>(column.name));
   const auto& string_column = table.getColumn<StringColumn>(column.name);
   return std::make_unique<filter::operators::Selection>(
      std::make_unique<filter::operators::StringInSet<StringColumn>>(
         &string_column, filter::operators::StringInSet<StringColumn>::Comparator::IN, values
      ),
      table.row_layout
   );
}

}  // namespace rhydb::query_engine::scalar_expressions
