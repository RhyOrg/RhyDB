#include "rhydb/query_engine/scalar_expressions/between_arrow.h"

#include <optional>
#include <string>
#include <vector>

#include <arrow/compute/api.h>
#include <arrow/datum.h>
#include <arrow/scalar.h>
#include <arrow/type.h>

namespace rhydb::query_engine::scalar_expressions {

arrow::compute::Expression betweenToArrowExpression(
   const std::string& column_name,
   const std::optional<arrow::Datum>& from,
   const std::optional<arrow::Datum>& to
) {
   const auto value = arrow::compute::field_ref(column_name);

   std::vector<arrow::compute::Expression> predicates;
   if (from.has_value()) {
      predicates.push_back(arrow::compute::greater_equal(value, arrow::compute::literal(*from)));
   }
   if (to.has_value()) {
      predicates.push_back(arrow::compute::less_equal(value, arrow::compute::literal(*to)));
   }

   if (predicates.empty()) {
      // Unbounded, so the filter is true on every row that is not null, and null on the others
      return arrow::compute::call(
         "if_else",
         {
            arrow::compute::is_valid(value),
            arrow::compute::literal(true),
            arrow::compute::literal(arrow::MakeNullScalar(arrow::boolean())),
         }
      );
   }
   if (predicates.size() == 1) {
      return predicates.front();
   }
   return arrow::compute::and_(predicates.front(), predicates.back());
}

}  // namespace rhydb::query_engine::scalar_expressions
