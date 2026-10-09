#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/array.h>
#include <arrow/compute/expression.h>
#include <arrow/result.h>

#include "rhydb/common/bitmap.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::exec_node {

/// Groups rows by the symbol they carry at a fixed sequence position. The position must be within
/// the sequence's reference (validated when the plan is built).
struct ResolvedSequencePositionDimension {
   std::string column_name;
   uint32_t position_idx;  // 0-based
   bool is_nucleotide;
};

/// Groups rows by the value of an indexed string column, straight from its inverted index.
struct ResolvedIndexedColumnDimension {
   std::string column_name;
};

/// Groups rows by the value of a scalar expression, evaluated via Arrow over the filtered rows.
struct ResolvedScalarExpressionDimension {
   /// The table columns the expression reads, with their real column types
   std::vector<schema::ColumnIdentifier> referenced_columns;
   /// The expression, bound against `columnsToArrowSchema(referenced_columns)`
   arrow::compute::Expression bound_expression;
   schema::ColumnType output_type;
};

/// One grouping dimension of a bitmap aggregation (`operators::GroupingDimension`), resolved
/// against the table for execution
using ResolvedGroupingDimension = std::variant<
   ResolvedSequencePositionDimension,
   ResolvedIndexedColumnDimension,
   ResolvedScalarExpressionDimension>;

/// The slice of a `DimensionIndex` covering one 2^16 chunk. Either every row of the chunk carries
/// the same value (SingletonValueDimension), so only that value's index entry is stored, or the
/// rows carry several values (ValueBitmapDimension), stored as one (entry, rows) pair per value.
using SingletonValueDimension = size_t;
using ValueBitmapDimension = std::vector<std::pair<size_t, roaring_util::CopyOnWriteContainer>>;

using DimensionIndexChunk = std::variant<ValueBitmapDimension, SingletonValueDimension>;

/// An inverted index on the values of one grouping dimension: for every value the dimension takes,
/// the rows carrying it. Entries are numbered 0..n-1 in output order (the null value last), and the
/// index is served one 2^16 chunk at a time, which is how the bitmap aggregation intersects it with
/// the other dimensions.
///
/// Implementations may hand out stored bitmaps unfiltered (e.g. an indexed column's own inverted
/// index), so an entry's rows can include rows outside the filter; the aggregation intersects them
/// with the filter.
class DimensionIndex {
  public:
   DimensionIndex() = default;
   DimensionIndex(const DimensionIndex&) = delete;
   DimensionIndex& operator=(const DimensionIndex&) = delete;
   DimensionIndex(DimensionIndex&&) = delete;
   DimensionIndex& operator=(DimensionIndex&&) = delete;
   virtual ~DimensionIndex() = default;

   /// The index entries for the rows of chunk `chunk_id`. `filter_view` is that chunk's filter,
   /// which an implementation may use to narrow the entries it computes.
   [[nodiscard]] virtual DimensionIndexChunk chunk(
      uint16_t chunk_id,
      roaring_util::RoaringContainerView filter_view
   ) const = 0;

   /// The indexed values: element i is the value of entry i
   [[nodiscard]] virtual arrow::Result<std::shared_ptr<arrow::Array>> values() const = 0;
};

/// Builds the inverted index of `dimension` over `table`. `filter_bitmap` bounds the rows a
/// scalar-expression dimension is evaluated over.
arrow::Result<std::unique_ptr<DimensionIndex>> buildDimensionIndex(
   const ResolvedGroupingDimension& dimension,
   const storage::Table& table,
   const Bitmap& filter_bitmap
);

}  // namespace rhydb::query_engine::exec_node
