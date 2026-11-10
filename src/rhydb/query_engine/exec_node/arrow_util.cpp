#include "rhydb/query_engine/exec_node/arrow_util.h"

#include <arrow/array/util.h>

namespace rhydb::query_engine::exec_node {

arrow::Result<std::shared_ptr<arrow::Array>> execValueAsArray(
   const arrow::compute::ExecValue& value,
   int64_t length,
   arrow::MemoryPool* pool
) {
   if (value.is_array()) {
      return value.array.ToArray();
   }
   return arrow::MakeArrayFromScalar(*value.scalar, length, pool);
}

std::shared_ptr<arrow::DataType> columnTypeToArrowType(const schema::DataType& column_type) {
   using Kind = schema::DataType::Kind;
   // A dictionary-encoded column is output as plain values
   switch (column_type.getPlainType().getKind()) {
      case Kind::STRING:
         return arrow::large_utf8();
      case Kind::DICTIONARY_ENCODED:
         RHYDB_UNREACHABLE();
      case Kind::DATE32:
         return arrow::date32();
      case Kind::BOOL:
         return arrow::boolean();
      case Kind::INT32:
         return arrow::int32();
      case Kind::INT64:
         return arrow::int64();
      case Kind::FLOAT:
         return arrow::float64();
      case Kind::AMINO_ACID_SEQUENCE:
      case Kind::NUCLEOTIDE_SEQUENCE:
      case Kind::ZSTD_COMPRESSED_STRING:
         return arrow::large_binary();
   }
   RHYDB_UNREACHABLE();
}

std::shared_ptr<arrow::Schema> columnsToArrowSchema(
   const std::vector<rhydb::schema::ColumnIdentifier>& columns
) {
   std::vector<std::shared_ptr<arrow::Field>> fields;
   for (const auto& [name, type] : columns) {
      auto arrow_type = columnTypeToArrowType(type);
      fields.emplace_back(std::make_shared<arrow::Field>(name, arrow_type));
   }
   return std::make_shared<arrow::Schema>(fields);
}

}  // namespace rhydb::query_engine::exec_node
