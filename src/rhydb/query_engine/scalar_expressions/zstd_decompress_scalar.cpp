#include "rhydb/query_engine/scalar_expressions/zstd_decompress_scalar.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arrow/compute/api.h>
#include <fmt/format.h>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/zstd_decompress_expression.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/schema/database_schema.h"

namespace rhydb::query_engine::scalar_expressions {

ZstdDecompressScalar::ZstdDecompressScalar(
   std::unique_ptr<ScalarExpression> input,
   std::string dictionary_string
)
    : input(std::move(input)),
      dictionary_string(std::move(dictionary_string)) {
   RHYDB_ASSERT(this->input != nullptr);
   RHYDB_ASSERT(!this->dictionary_string.empty());
}

std::unique_ptr<ScalarExpression> ZstdDecompressScalar::clone() const {
   return std::make_unique<ZstdDecompressScalar>(input->clone(), dictionary_string);
}

std::string ZstdDecompressScalar::toString() const {
   return fmt::format("zstd_decompress({})", input->toString());
}

std::vector<schema::ColumnIdentifier> ZstdDecompressScalar::freeIUs() const {
   return input->freeIUs();
}

arrow::Result<arrow::compute::Expression> ZstdDecompressScalar::toArrowExpression() const {
   ARROW_ASSIGN_OR_RAISE(auto input_expression, input->toArrowExpression());
   return exec_node::ZstdDecompressExpression::make(input_expression, dictionary_string);
}

std::unique_ptr<ScalarExpression> ZstdDecompressScalar::rewrite(
   const storage::Table& table,
   AmbiguityMode mode
) const {
   return std::make_unique<ZstdDecompressScalar>(input->rewrite(table, mode), dictionary_string);
}

std::unique_ptr<filter::operators::Operator> ZstdDecompressScalar::compile(
   const storage::Table& /*table*/,
   bool /*with_null_rows*/
) const {
   // ZstdDecompressScalar is a scalar expression, not a filter predicate.
   RHYDB_UNIMPLEMENTED();
}

}  // namespace rhydb::query_engine::scalar_expressions
