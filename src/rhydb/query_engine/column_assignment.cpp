#include "rhydb/query_engine/column_assignment.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <arrow/array/concatenate.h>
#include <arrow/array/util.h>
#include <arrow/builder.h>
#include <arrow/compute/api.h>
#include <arrow/type_traits.h>
#include <fmt/format.h>
#include <roaring/roaring.hh>

#include "rhydb/common/bitmap.h"
#include "rhydb/common/panic.h"
#include "rhydb/query_engine/exec_node/arrow_batch_sink.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/project_node.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/query_engine/query_plan.h"
#include "rhydb/query_engine/saneql/ast_to_query.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/storage/column/update_chunks.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine {

namespace {

using schema::DataType;

// Computing values goes through arrow::Result; this raises its failures (e.g. an integer that does
// not fit the column it is cast to) as query errors.
template <typename T>
T orThrowQuery(arrow::Result<T> result) {
   CHECK_RHYDB_QUERY(result.ok(), "{}", result.status().ToString());
   return std::move(result).ValueOrDie();
}

template <typename Column>
constexpr bool IS_SEQUENCE_COLUMN = Column::KIND == DataType::Kind::NUCLEOTIDE_SEQUENCE ||
                                    Column::KIND == DataType::Kind::AMINO_ACID_SEQUENCE;

/// The type of the values a column of `column_type` holds, as queries see them: indexed and
/// compressed string columns hold plain strings.
DataType valueTypeOf(const DataType& column_type) {
   if (column_type == DataType::ZSTD_COMPRESSED_STRING) {
      return DataType::STRING;
   }
   return column_type.getPlainType();
}

/// Whether a value of `value_type` can be assigned to a column of `column_type`. Integers of either
/// width fit every numeric column; everything else must match the column's value type.
bool isAssignable(const DataType& value_type, const DataType& column_type) {
   const DataType target_type = valueTypeOf(column_type);
   if (valueTypeOf(value_type) == target_type) {
      return true;
   }
   const bool is_integer = value_type == DataType::INT32 || value_type == DataType::INT64;
   return is_integer && (target_type == DataType::INT32 || target_type == DataType::INT64 ||
                         target_type == DataType::FLOAT);
}

void checkColumnIsUpdatable(const storage::Table& table, const schema::ColumnIdentifier& column) {
   std::visit(
      [&]<typename Column>(const Column& column_data) {
         if constexpr (IS_SEQUENCE_COLUMN<Column>) {
            throw IllegalQueryException(
               "Updating columns of type '{}' is not supported (column '{}')",
               schema::dataTypeToString(column.type),
               column.name
            );
         } else if constexpr (std::is_same_v<Column, storage::column::StringColumn>) {
            CHECK_RHYDB_QUERY(
               !column_data.metadata->phylo_tree.has_value(),
               "Column '{}' is backed by a phylogenetic tree and cannot be updated, because it "
               "would break the tree's row bindings",
               column.name
            );
         }
      },
      table.columns.at(column.name)
   );
}

/// The values an assignment computed for the updated rows: element `i` belongs to the `i`-th
/// updated row, or a single element belongs to all of them when the value reads no column.
struct ComputedValues {
   std::shared_ptr<arrow::Array> values;
   bool is_constant;
};

std::shared_ptr<arrow::Array> castToColumn(
   std::shared_ptr<arrow::Array> values,
   const schema::ColumnIdentifier& column
) {
   const auto column_type = exec_node::columnTypeToArrowType(valueTypeOf(column.type));
   if (values->type()->Equals(*column_type)) {
      return values;
   }
   return orThrowQuery(arrow::compute::Cast(values, column_type)).make_array();
}

/// Computes a value that reads no column once, instead of once per row.
ComputedValues computeConstant(const ColumnAssignment& assignment) {
   const auto expression =
      orThrowQuery(orThrowQuery(assignment.value->toArrowExpression()).Bind(arrow::Schema{{}}));
   const auto value =
      orThrowQuery(arrow::compute::ExecuteScalarExpression(expression, arrow::ExecBatch{{}, 1}));
   auto values = value.is_scalar() ? orThrowQuery(arrow::MakeArrayFromScalar(*value.scalar(), 1))
                                   : value.make_array();
   return {.values = castToColumn(std::move(values), assignment.column), .is_constant = true};
}

/// Collects the columns of a query result, batch by batch.
class ValuesSink : public exec_node::ArrowBatchSink {
  public:
   std::vector<arrow::ArrayVector> columns;

   explicit ValuesSink(size_t column_count)
       : columns(column_count) {}

   arrow::Status writeBatch(const arrow::compute::ExecBatch& batch) override {
      RHYDB_ASSERT_EQ(batch.values.size(), columns.size());
      for (size_t index = 0; index < columns.size(); ++index) {
         const auto& datum = batch.values.at(index);
         if (datum.is_array()) {
            columns.at(index).push_back(datum.make_array());
         } else {
            ARROW_ASSIGN_OR_RAISE(
               auto array, arrow::MakeArrayFromScalar(*datum.scalar(), batch.length)
            );
            columns.at(index).push_back(std::move(array));
         }
      }
      return arrow::Status::OK();
   }

   arrow::Status finish() override { return arrow::Status::OK(); }
};

using ChunkRows = std::vector<std::pair<uint16_t, roaring_util::RoaringContainerView>>;

/// A builder of the column's storage representation (e.g. compressed values)
template <typename Column>
Column::Builder makeBuilder(Column& column_data) {
   if constexpr (std::is_constructible_v<typename Column::Builder, typename Column::Metadata*>) {
      return typename Column::Builder{column_data.metadata};
   } else {
      return typename Column::Builder{};
   }
}

/// Builds one `Buffer` per chunk of `chunks`. The values are in ascending row id order, so each
/// chunk's rows take the next values of `value_at`.
template <typename Column, typename ValueAt>
std::vector<typename Column::Buffer> buildChunkBuffers(
   Column& column_data,
   const ChunkRows& chunks,
   const ValueAt& value_at
) {
   std::vector<typename Column::Buffer> buffers;
   buffers.reserve(chunks.size());
   int64_t index = 0;
   for (const auto& [chunk_id, chunk_rows] : chunks) {
      auto builder = makeBuilder(column_data);
      for (uint32_t row = 0; row < chunk_rows.getCardinality(); ++row) {
         auto value = value_at(index++);
         if (value.has_value()) {
            builder.insert(*value);
         } else {
            builder.insertNull();
         }
      }
      buffers.push_back(builder.finalize());
   }
   return buffers;
}

/// Converts the computed values into the column's `Buffer`s, one per chunk of `chunks` (the chunks
/// of `row_ids`), and returns the write of them. Everything that can fail happens here, so every
/// assignment can be converted before any is written.
std::function<void()> prepareWrite(
   storage::Table& table,
   const schema::ColumnIdentifier& column,
   const roaring::Roaring& row_ids,
   const ChunkRows& chunks,
   const ComputedValues& computed
) {
   return std::visit(
      [&]<typename Column>(Column& column_data) -> std::function<void()> {
         if constexpr (IS_SEQUENCE_COLUMN<Column>) {
            // Rejected by checkColumnIsUpdatable
            RHYDB_UNREACHABLE();
         } else {
            // The values were cast to the Arrow type the column is read as, so they are an array
            // of the type the column's Arrow builder produces.
            using ArrayType =
               arrow::TypeTraits<typename exec_node::ArrowBuilder<Column>::TypeClass>::ArrayType;
            const auto& values = static_cast<const ArrayType&>(*computed.values);
            using Value = Column::Buffer::value_type::value_type;
            auto value_at = [&](int64_t index) -> std::optional<Value> {
               if (values.IsNull(index)) {
                  return std::nullopt;
               }
               return Value{values.GetView(index)};
            };
            if (computed.is_constant) {
               return [&column_data, &row_ids, value = value_at(0)] {
                  column_data.update(row_ids, value);
               };
            }
            auto buffers = buildChunkBuffers(column_data, chunks, value_at);
            return [&column_data, &chunks, buffers = std::move(buffers)] {
               storage::column::updateChunks(column_data, chunks, buffers);
            };
         }
      },
      table.columns.at(column.name)
   );
}

}  // namespace

ColumnAssignment makeColumnAssignment(
   const std::shared_ptr<storage::Table>& table,
   const schema::ColumnIdentifier& column,
   const saneql::ast::Expression& value,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables
) {
   checkColumnIsUpdatable(*table, column);

   // `null` has no type of its own; it takes the column's
   if (saneql::ast::isNullLiteral(value)) {
      return {
         .column = column,
         .value = std::make_unique<scalar_expressions::NullLiteral>(valueTypeOf(column.type)),
      };
   }

   // The value is computed by a query over the table, so it sees the columns as queries do
   const auto columns =
      saneql::makeTableScan(table, std::make_unique<scalar_expressions::BoolLiteral>(true))
         ->getOutputSchema();
   const auto error_context = fmt::format("update() value of column '{}'", column.name);
   auto expression = saneql::convertToScalar(value, columns, error_context, tables);
   CHECK_RHYDB_QUERY(
      isAssignable(expression->type(), column.type),
      "{} has type '{}', which cannot be assigned to a column of type '{}'",
      error_context,
      schema::dataTypeToString(expression->type()),
      schema::dataTypeToString(column.type)
   );
   return {.column = column, .value = std::move(expression)};
}

size_t applyColumnAssignments(
   const std::shared_ptr<storage::Table>& table,
   const std::vector<ColumnAssignment>& assignments,
   const scalar_expressions::ScalarExpression& predicate,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& tables,
   const config::QueryOptions& query_options,
   std::string_view request_id
) {
   const roaring::Roaring row_ids =
      predicate.rewrite(*table, scalar_expressions::ScalarExpression::AmbiguityMode::NONE)
         ->compile(*table)
         ->evaluate()
         .toRoaring();

   // Values that read columns are computed per row by one query, which scans the same rows in
   // ascending row id order, so its `i`-th result row belongs to the `i`-th updated row. Each value
   // is named after the column it is assigned to.
   std::vector<ComputedValues> computed(assignments.size());
   std::vector<operators::MapNode::Assignment> per_row_values;
   std::vector<schema::ColumnIdentifier> per_row_columns;
   std::vector<size_t> per_row_assignments;
   for (size_t index = 0; index < assignments.size(); ++index) {
      const auto& assignment = assignments.at(index);
      if (assignment.value->freeIUs().empty()) {
         computed.at(index) = computeConstant(assignment);
         continue;
      }
      const schema::ColumnIdentifier output{
         .name = assignment.column.name,
         .type = assignment.value->type(),
      };
      per_row_values.push_back({.output_column = output, .expression = assignment.value->clone()});
      per_row_columns.push_back(output);
      per_row_assignments.push_back(index);
   }

   if (!per_row_values.empty()) {
      auto query = std::make_unique<operators::ProjectNode>(
         std::make_unique<operators::MapNode>(
            saneql::makeTableScan(table, predicate.clone()), std::move(per_row_values)
         ),
         per_row_columns
      );
      auto query_plan = Planner::planQuery(std::move(query), tables, query_options, request_id);
      // The result rows are only matched to the updated rows by their order
      RHYDB_ASSERT(query_plan.result_ordering.is_implicit());
      ValuesSink sink{per_row_columns.size()};
      constexpr uint64_t DEFAULT_TIMEOUT_SECONDS = 120;
      query_plan.executeAndWrite(sink, DEFAULT_TIMEOUT_SECONDS);

      for (size_t column = 0; column < per_row_columns.size(); ++column) {
         const auto& chunks = sink.columns.at(column);
         auto values = chunks.empty()
                          ? orThrowQuery(
                               arrow::MakeEmptyArray(
                                  exec_node::columnTypeToArrowType(per_row_columns.at(column).type)
                               )
                            )
                          : orThrowQuery(arrow::Concatenate(chunks));
         RHYDB_ASSERT_EQ(static_cast<uint64_t>(values->length()), row_ids.cardinality());
         const size_t index = per_row_assignments.at(column);
         computed.at(index) = {
            .values = castToColumn(std::move(values), assignments.at(index).column),
            .is_constant = false,
         };
      }
   }

   ChunkRows chunks;
   for (const auto& chunk : Bitmap{&row_ids}) {
      chunks.push_back(chunk);
   }
   std::vector<std::function<void()>> writes;
   writes.reserve(assignments.size());
   for (size_t index = 0; index < assignments.size(); ++index) {
      writes.push_back(
         prepareWrite(*table, assignments.at(index).column, row_ids, chunks, computed.at(index))
      );
   }
   for (const auto& write : writes) {
      write();
   }
   return row_ids.cardinality();
}

}  // namespace rhydb::query_engine
