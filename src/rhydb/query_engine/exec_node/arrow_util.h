#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <arrow/array/array_base.h>
#include <arrow/compute/exec.h>
#include <arrow/result.h>
#include <arrow/type.h>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/bool_column.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/float_column.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

namespace rhydb::query_engine::exec_node {

std::shared_ptr<arrow::DataType> columnTypeToArrowType(const schema::DataType& column_type);

std::shared_ptr<arrow::Schema> columnsToArrowSchema(
   const std::vector<rhydb::schema::ColumnIdentifier>& columns
);

/// The kernel input as an array, also when Arrow passes a scalar (e.g. for a constant argument).
/// Shared by the custom cast kernels, whose logic operates on arrays.
arrow::Result<std::shared_ptr<arrow::Array>> execValueAsArray(
   const arrow::compute::ExecValue& value,
   int64_t length,
   arrow::MemoryPool* pool
);

template <storage::column::Column Column>
struct ArrowBuilderSelector;

template <>
struct ArrowBuilderSelector<storage::column::StringColumn> {
   using builder_type = arrow::LargeStringBuilder;
   using value_type = std::string;
};

/// A dictionary-encoded column is output like a plain column of its dictionary values
template <storage::column::DictionaryValueColumn ValueColumn>
struct ArrowBuilderSelector<storage::column::DictionaryEncodedColumn<ValueColumn>>
    : ArrowBuilderSelector<ValueColumn> {};

template <>
struct ArrowBuilderSelector<storage::column::SequenceColumn<Nucleotide>> {
   using builder_type = arrow::LargeBinaryBuilder;
   using value_type = std::string;
};

template <>
struct ArrowBuilderSelector<storage::column::SequenceColumn<AminoAcid>> {
   using builder_type = arrow::LargeBinaryBuilder;
   using value_type = std::string;
};

template <>
struct ArrowBuilderSelector<storage::column::ZstdCompressedStringColumn> {
   using builder_type = arrow::LargeBinaryBuilder;
   using value_type = std::string;
};

template <>
struct ArrowBuilderSelector<storage::column::FloatColumn> {
   using builder_type = arrow::DoubleBuilder;
   using value_type = double;
};

template <>
struct ArrowBuilderSelector<storage::column::BoolColumn> {
   using builder_type = arrow::BooleanBuilder;
   using value_type = bool;
};

template <>
struct ArrowBuilderSelector<storage::column::Int32Column> {
   using builder_type = arrow::Int32Builder;
   using value_type = int32_t;
};

template <>
struct ArrowBuilderSelector<storage::column::Int64Column> {
   using builder_type = arrow::Int64Builder;
   using value_type = int64_t;
};

template <>
struct ArrowBuilderSelector<storage::column::Date32Column> {
   using builder_type = arrow::Date32Builder;
   using value_type = int32_t;
};

template <storage::column::Column ColumnType>
using ArrowBuilder = ArrowBuilderSelector<ColumnType>::builder_type;

}  // namespace rhydb::query_engine::exec_node