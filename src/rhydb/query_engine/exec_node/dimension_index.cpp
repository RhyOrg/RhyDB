#include "rhydb/query_engine/exec_node/dimension_index.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/array.h>
#include <arrow/array/util.h>
#include <arrow/builder.h>
#include <arrow/compute/api.h>
#include <arrow/compute/expression.h>
#include <arrow/datum.h>
#include <arrow/result.h>
#include <roaring/roaring.hh>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/symbol_map.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/table_scan.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::exec_node {

namespace {

using roaring_util::CopyOnWriteContainer;
using roaring_util::RoaringContainer;
using roaring_util::RoaringContainerView;

/// Groups the rows by the symbol they carry at a fixed sequence position
/// Introduced as a speed-up when `<seq>.at(<position>)` was detected as group key expression
template <typename SymbolType>
class SequencePositionIndex : public DimensionIndex {
   static constexpr size_t SYMBOL_COUNT = SymbolType::SYMBOLS.size();
   // The null group's index sits just past every symbol
   static constexpr size_t NULL_INDEX = SYMBOL_COUNT;

   const storage::column::SequenceColumn<SymbolType>& column;
   uint32_t position_idx;
   SymbolType::Symbol reference_symbol;
   SymbolType::Symbol missing_symbol = SymbolType::SYMBOL_MISSING;

   // The position's vertical mutation containers, bucketed by 2^16 chunk so the per-chunk work is a
   // lookup rather than a rescan of the position's whole diff range. Views into the stored index.
   std::map<uint16_t, std::vector<std::pair<typename SymbolType::Symbol, RoaringContainerView>>>
      mutations_by_chunk;
   // The column's null rows, one view per 2^16 chunk (into `column.null_bitmap`).
   std::map<uint16_t, RoaringContainerView> null_views;

  public:
   SequencePositionIndex(
      const storage::column::SequenceColumn<SymbolType>& column,
      uint32_t position_idx
   )
       : column(column),
         position_idx(position_idx) {
      RHYDB_ASSERT_LT(position_idx, column.metadata->reference_sequence.size());
      reference_symbol = column.getLocalReferencePosition(position_idx);

      auto [diff_it, diff_end] = column.vertical_sequence_index.getRangeForPosition(position_idx);
      for (; diff_it != diff_end; ++diff_it) {
         mutations_by_chunk[diff_it->first.v_index].emplace_back(
            diff_it->first.symbol, RoaringContainerView(diff_it->second)
         );
      }

      for (const auto& [chunk_id, view] : Bitmap{&column.null_bitmap}) {
         null_views.emplace(chunk_id, view);
      }
   }

   struct ChunkMutationContainers {
      SymbolMap<SymbolType, std::optional<RoaringContainerView>> views;
      CopyOnWriteContainer except_missing;
      CopyOnWriteContainer except_reference_and_missing;
   };

   [[nodiscard]] ChunkMutationContainers computeChunkMutationContainers(size_t chunk_id) const {
      SymbolMap<SymbolType, std::optional<RoaringContainerView>> mutation_views;
      // The reference symbol is never itself a stored mutation, so excluding it is a no-op in
      // practice, but it is kept explicit to mirror the generic path exactly. Each accumulator
      // starts empty (borrowing nothing) and copy-on-writes a private owning union on the first
      // `|=`.
      CopyOnWriteContainer except_missing;
      CopyOnWriteContainer except_reference_and_missing;
      if (auto chunk_mutations = mutations_by_chunk.find(chunk_id);
          chunk_mutations != mutations_by_chunk.end()) {
         for (const auto& [symbol, view] : chunk_mutations->second) {
            mutation_views[symbol] = view;
            if (symbol != missing_symbol) {
               except_missing |= view;
               if (symbol != reference_symbol) {
                  except_reference_and_missing |= view;
               }
            }
         }
      }
      return ChunkMutationContainers{
         .views = std::move(mutation_views),
         .except_missing = std::move(except_missing),
         .except_reference_and_missing = std::move(except_reference_and_missing),
      };
   }

   // NOLINTNEXTLINE(readability-function-cognitive-complexity)
   [[nodiscard]] DimensionIndexChunk chunk(uint16_t chunk_id, RoaringContainerView filter_view)
      const override {
      const auto& coverage = column.horizontal_coverage_index;
      const bool chunk_has_mutations = mutations_by_chunk.contains(chunk_id);

      // fast path if chunk does not have any mutations
      if (!chunk_has_mutations) {
         // No row covers the position -> every row is missing. A null row would form its own group,
         // so only collapse when the chunk has no nulls.
         if (!null_views.contains(chunk_id) &&
             coverage.noRowCoversPositionInChunk(position_idx, chunk_id)) {
            return static_cast<size_t>(missing_symbol);
         }
         // Every row covers the position with no in-region N -> every row is the reference symbol.
         // (A null row forces the covered envelope empty, so this never fires with nulls.)
         if (coverage.positionCoveredByWholeChunk(position_idx, chunk_id)) {
            return static_cast<size_t>(reference_symbol);
         }
      }

      ValueBitmapDimension groups;

      // The chunk's per-symbol mutation containers (raw views)
      ChunkMutationContainers mutations = computeChunkMutationContainers(chunk_id);

      // The chunk's exact row-ids where the symbol is not N
      const RoaringContainer covered = coverage.coveredRowsInChunk(position_idx, chunk_id);

      // A null sequence carries no symbol at any position, so it forms its own group (a zero-copy
      // view, handed out un-filtered like the mutation groups) and is excluded from the missing
      // symbol below. The computed missing/reference groups are already subsets of the filter, so
      // subtracting the whole chunk's nulls there is equivalent to subtracting only the filtered
      // ones.
      std::optional<RoaringContainerView> null_view;
      if (auto null_iter = null_views.find(chunk_id); null_iter != null_views.end()) {
         null_view = null_iter->second;
      }

      for (auto symbol : SymbolType::SYMBOLS) {
         auto symbol_rank = static_cast<size_t>(symbol);
         const bool is_reference = symbol == reference_symbol;
         const bool is_missing = symbol == missing_symbol;

         if (!is_reference && !is_missing) {
            // Plain mutation: hand the stored container straight through, no copy and no filtering
            // (the aggregation intersects the filter in).
            if (mutations.views[symbol].has_value()) {
               groups.emplace_back(symbol_rank, CopyOnWriteContainer{*mutations.views[symbol]});
            }
            continue;
         }

         CopyOnWriteContainer group;

         if (is_reference && is_missing) {
            // The local reference symbol is itself the missing symbol: every filtered row without
            // an explicit other mutation carries it, minus the sequence-less rows.
            group = CopyOnWriteContainer{filter_view - mutations.except_missing.view()};
            if (null_view.has_value()) {
               group -= *null_view;
            }
         } else if (is_missing) {
            group = CopyOnWriteContainer{
               filter_view - RoaringContainerView{covered},
            };  // not covered here ...
            if (mutations.views[symbol].has_value()) {
               // ... plus any explicit missing mutation (bounded by the filter) ...
               const RoaringContainer explicit_missing = filter_view & *mutations.views[symbol];
               group |= RoaringContainerView{explicit_missing};
            }
            // ... but a null sequence carries no symbol.
            if (null_view.has_value()) {
               group -= *null_view;
            }
         } else {  // is_reference
            // covered ...
            const RoaringContainer covered_in_filter = filter_view & RoaringContainerView{covered};
            // ... and carrying no other mutation.
            group = CopyOnWriteContainer{
               RoaringContainerView{covered_in_filter} -
                  mutations.except_reference_and_missing.view(),
            };
         }
         if (!group.empty()) {
            groups.emplace_back(symbol_rank, std::move(group));
         }
      }
      if (null_view.has_value()) {
         groups.emplace_back(NULL_INDEX, CopyOnWriteContainer{*null_view});
      }
      return groups;
   }

   /// One 1-character string per symbol
   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Array>> values() const override {
      arrow::StringBuilder builder;
      for (const auto symbol : SymbolType::SYMBOLS) {
         ARROW_RETURN_NOT_OK(builder.Append(std::string(1, SymbolType::symbolToChar(symbol))));
      }
      ARROW_RETURN_NOT_OK(builder.AppendNull());  // NULL_INDEX
      std::shared_ptr<arrow::Array> array;
      ARROW_RETURN_NOT_OK(builder.Finish(&array));
      return array;
   }
};

/// Groups the rows by the value of an indexed string column, straight from its inverted index. Each
/// value's rows are handed out as zero-copy views into the stored per-value bitmap (one per 2^16
/// chunk), plus a null group from the column's null bitmap. Value groups get consecutive indices in
/// sorted-value order (null last), so the combinations come out in the same order as the generic
/// path.
class IndexedColumnIndex : public DimensionIndex {
   // chunk key -> the groups holding a container in that chunk. Precomputed once (the inverted
   // index is unordered), so serving a chunk is a single map lookup returning views into stored
   // bitmaps.
   std::map<uint16_t, ValueBitmapDimension> groups_by_chunk;
   // The distinct value of each group index, in sorted order (the null group has no entry; it is
   // the trailing null appended by `values`).
   std::vector<std::string> key_values;

  public:
   explicit IndexedColumnIndex(const storage::column::DictionaryEncodedColumn& column) {
      // One group per distinct value, ordered by the value string so the node has a deterministic
      // output order. A null row lives only in `null_bitmap` (its value's bitmap does not contain
      // it), so the null group stays disjoint from the value groups and no row is double-counted.
      std::vector<std::pair<std::string, const roaring::Roaring*>> values;
      values.reserve(column.getIndexedValues().size());
      for (const auto& [value_id, value_bitmap] : column.getIndexedValues()) {
         values.emplace_back(std::string{column.lookupValue(value_id)}, &value_bitmap);
      }
      std::ranges::sort(values, [](const auto& lhs, const auto& rhs) {
         return lhs.first < rhs.first;
      });

      key_values.reserve(values.size());
      for (size_t group_index = 0; group_index < values.size(); ++group_index) {
         for (const auto& [chunk_id, view] : Bitmap{values[group_index].second}) {
            groups_by_chunk[chunk_id].emplace_back(group_index, CopyOnWriteContainer{view});
         }
         key_values.emplace_back(std::move(values[group_index].first));
      }
      const size_t null_index = values.size();
      for (const auto& [chunk_id, view] : Bitmap{&column.null_bitmap}) {
         groups_by_chunk[chunk_id].emplace_back(null_index, CopyOnWriteContainer{view});
      }
   }

   [[nodiscard]] DimensionIndexChunk chunk(uint16_t chunk_id, RoaringContainerView /*filter_view*/)
      const override {
      if (auto iter = groups_by_chunk.find(chunk_id); iter != groups_by_chunk.end()) {
         return iter->second;
      }
      return ValueBitmapDimension{};
   }

   /// The distinct values in sorted order (the group indices `chunk` hands out), then a
   /// null for the trailing null group.
   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Array>> values() const override {
      arrow::StringBuilder builder;
      for (const auto& value : key_values) {
         ARROW_RETURN_NOT_OK(builder.Append(value));
      }
      ARROW_RETURN_NOT_OK(builder.AppendNull());  // null group
      std::shared_ptr<arrow::Array> array;
      ARROW_RETURN_NOT_OK(builder.Finish(&array));
      return array;
   }
};

/// Binds one Arrow value type to the C++ key it is bucketed by and the builder that reproduces it,
/// so `buildScalarGroups` is written once and instantiated per output type.
struct StringValueTraits {
   using ArrayType = arrow::StringArray;
   using BuilderType = arrow::StringBuilder;
   using KeyType = std::string;
   static KeyType key(const ArrayType& array, int64_t index) { return array.GetString(index); }
   static arrow::Status append(BuilderType& builder, const KeyType& value) {
      return builder.Append(value);
   }
};
struct Int32ValueTraits {
   using ArrayType = arrow::Int32Array;
   using BuilderType = arrow::Int32Builder;
   using KeyType = int32_t;
   static KeyType key(const ArrayType& array, int64_t index) { return array.Value(index); }
   static arrow::Status append(BuilderType& builder, KeyType value) {
      return builder.Append(value);
   }
};
struct Int64ValueTraits {
   using ArrayType = arrow::Int64Array;
   using BuilderType = arrow::Int64Builder;
   using KeyType = int64_t;
   static KeyType key(const ArrayType& array, int64_t index) { return array.Value(index); }
   static arrow::Status append(BuilderType& builder, KeyType value) {
      return builder.Append(value);
   }
};
struct DoubleValueTraits {
   using ArrayType = arrow::DoubleArray;
   using BuilderType = arrow::DoubleBuilder;
   using KeyType = double;
   static KeyType key(const ArrayType& array, int64_t index) { return array.Value(index); }
   static arrow::Status append(BuilderType& builder, KeyType value) {
      return builder.Append(value);
   }
};
struct BoolValueTraits {
   using ArrayType = arrow::BooleanArray;
   using BuilderType = arrow::BooleanBuilder;
   using KeyType = bool;
   static KeyType key(const ArrayType& array, int64_t index) { return array.Value(index); }
   static arrow::Status append(BuilderType& builder, KeyType value) {
      return builder.Append(value);
   }
};
struct Date32ValueTraits {
   using ArrayType = arrow::Date32Array;
   using BuilderType = arrow::Date32Builder;
   using KeyType = int32_t;  // days since epoch; sorts chronologically
   static KeyType key(const ArrayType& array, int64_t index) { return array.Value(index); }
   static arrow::Status append(BuilderType& builder, KeyType value) {
      return builder.Append(value);
   }
};

/// Materializes `referenced` for the rows chunk `chunk_id` holds in `chunk_rows`, evaluates the
/// bound `expression` over them and returns the value array (length == the view's cardinality),
/// cast to `output_type`. Row `i` of the returned array is the `i`-th row id of the view, because
/// the materialized rows are appended in ascending row-id order. The view is handed to the batch
/// builder as a borrowed single-block bitmap, so nothing is cloned to describe the row set.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<std::shared_ptr<arrow::Array>> evaluateExpressionForRows(
   const storage::Table& table,
   const std::vector<schema::ColumnIdentifier>& referenced,
   const arrow::compute::Expression& bound_expression,
   const std::shared_ptr<arrow::DataType>& output_type,
   uint16_t chunk_id,
   RoaringContainerView chunk_rows,
   arrow::compute::ExecContext& exec_context
) {
   const auto row_count = static_cast<int64_t>(chunk_rows.getCardinality());

   ExecBatchBuilder batch_builder{referenced};
   ARROW_RETURN_NOT_OK(
      batch_builder.appendEntries(table, Bitmap::fromContainerViews({{chunk_id, chunk_rows}}))
   );
   ARROW_ASSIGN_OR_RAISE(auto batch, batch_builder.finishBatch(row_count));

   ARROW_ASSIGN_OR_RAISE(
      auto datum, arrow::compute::ExecuteScalarExpression(bound_expression, batch, &exec_context)
   );
   std::shared_ptr<arrow::Array> array;
   if (datum.is_array()) {
      array = datum.make_array();
   } else if (datum.is_scalar()) {
      ARROW_ASSIGN_OR_RAISE(array, arrow::MakeArrayFromScalar(*datum.scalar(), row_count));
   } else {
      return arrow::Status::Invalid("scalar expression evaluated to neither an array nor a scalar");
   }
   if (!array->type()->Equals(*output_type)) {
      ARROW_ASSIGN_OR_RAISE(auto casted, arrow::compute::Cast(array, output_type));
      array = casted.make_array();
   }
   return array;
}

/// The per-value bitmaps, per-chunk group views and typed value array a `ScalarExpressionIndex`
/// serves, built once up front.
struct ScalarGroupData {
   // Owned per-value bitmaps (sorted values, then the null group), so the views below never dangle.
   std::vector<roaring::Roaring> value_bitmaps;
   std::map<uint16_t, ValueBitmapDimension> groups_by_chunk;
   // One element per group index: element i is group i's value, with a trailing null for the null
   // group. Its Arrow type is the dimension's output type.
   std::shared_ptr<arrow::Array> group_value_array;
};

/// Evaluates the expression chunk by chunk -- over the filtered rows only, since a row the filter
/// excludes can never contribute to a combination -- buckets every evaluated row's global id under
/// its (typed) value (nulls into their own group), then assigns group indices in sorted-value order
/// (null last) and builds the per-chunk views and the value array. Templated on the value type via
/// `Traits`.
template <typename Traits>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
arrow::Result<ScalarGroupData> buildScalarGroups(
   const storage::Table& table,
   const std::vector<schema::ColumnIdentifier>& referenced,
   const arrow::compute::Expression& bound_expression,
   const std::shared_ptr<arrow::DataType>& output_type,
   const Bitmap& filter_bitmap
) {
   arrow::compute::ExecContext exec_context;
   std::map<typename Traits::KeyType, roaring::Roaring> bitmap_by_value;
   roaring::Roaring null_bitmap;
   for (const auto& [chunk_id, filter_view] : filter_bitmap) {
      ARROW_ASSIGN_OR_RAISE(
         const std::shared_ptr<arrow::Array> array,
         evaluateExpressionForRows(
            table, referenced, bound_expression, output_type, chunk_id, filter_view, exec_context
         )
      );
      const auto& typed = static_cast<const Traits::ArrayType&>(*array);
      if (typed.length() != static_cast<int64_t>(filter_view.getCardinality())) {
         return arrow::Status::Invalid(
            "scalar expression produced ",
            typed.length(),
            " values for ",
            filter_view.getCardinality(),
            " rows"
         );
      }
      // The evaluated values and the view's row ids are both in ascending row-id order, so walking
      // them in lockstep pairs each value with the row it came from -- no row-id list to build.
      const uint32_t base = static_cast<uint32_t>(chunk_id) << 16U;
      int64_t index = 0;
      for (const uint16_t low_bits : filter_view) {
         const uint32_t global_row_id = base | low_bits;
         if (typed.IsNull(index)) {
            null_bitmap.add(global_row_id);
         } else {
            bitmap_by_value[Traits::key(typed, index)].add(global_row_id);
         }
         ++index;
      }
   }

   ScalarGroupData data;
   typename Traits::BuilderType value_builder;
   // Reserve distinct values + the null group so the container views taken below never dangle
   // across a reallocation of `value_bitmaps`.
   data.value_bitmaps.reserve(bitmap_by_value.size() + 1);
   for (auto& [value, bitmap] : bitmap_by_value) {
      const size_t group_index = data.value_bitmaps.size();
      data.value_bitmaps.push_back(std::move(bitmap));
      ARROW_RETURN_NOT_OK(Traits::append(value_builder, value));
      for (const auto& [chunk_id, view] : Bitmap{&data.value_bitmaps.back()}) {
         data.groups_by_chunk[chunk_id].emplace_back(group_index, CopyOnWriteContainer{view});
      }
   }
   const size_t null_index = data.value_bitmaps.size();
   data.value_bitmaps.push_back(std::move(null_bitmap));
   ARROW_RETURN_NOT_OK(value_builder.AppendNull());
   for (const auto& [chunk_id, view] : Bitmap{&data.value_bitmaps.back()}) {
      data.groups_by_chunk[chunk_id].emplace_back(null_index, CopyOnWriteContainer{view});
   }
   ARROW_ASSIGN_OR_RAISE(data.group_value_array, value_builder.Finish());
   return data;
}

/// Groups rows by the value of a map-produced scalar expression (e.g. `map({week :=
/// date.isoWeek()})`). Everything is precomputed in `buildScalarGroups` -- over the filtered rows
/// only, so neither the bitmaps nor the distinct values cover more than the query touches;
/// serving a chunk is a single map lookup, and `values` returns the typed value array
/// so the output column keeps the expression's type.
class ScalarExpressionIndex : public DimensionIndex {
   ScalarGroupData data;

  public:
   explicit ScalarExpressionIndex(ScalarGroupData data)
       : data(std::move(data)) {}

   [[nodiscard]] DimensionIndexChunk chunk(uint16_t chunk_id, RoaringContainerView /*filter_view*/)
      const override {
      if (auto iter = data.groups_by_chunk.find(chunk_id); iter != data.groups_by_chunk.end()) {
         return iter->second;
      }
      return ValueBitmapDimension{};
   }

   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Array>> values() const override {
      return data.group_value_array;
   }
};

template <typename SymbolType>
std::unique_ptr<DimensionIndex> buildSequencePositionIndex(
   const storage::Table& table,
   const ResolvedSequencePositionDimension& dimension
) {
   const auto& column = table.getColumn<typename SymbolType::Column>(dimension.column_name);
   return std::make_unique<SequencePositionIndex<SymbolType>>(column, dimension.position_idx);
}

template <typename Traits>
arrow::Result<std::unique_ptr<DimensionIndex>> buildScalarExpressionIndex(
   const storage::Table& table,
   const ResolvedScalarExpressionDimension& dimension,
   const Bitmap& filter_bitmap
) {
   const auto output_type = columnTypeToArrowType(dimension.output_type);
   ARROW_ASSIGN_OR_RAISE(
      auto data,
      buildScalarGroups<Traits>(
         table, dimension.referenced_columns, dimension.bound_expression, output_type, filter_bitmap
      )
   );
   return std::make_unique<ScalarExpressionIndex>(std::move(data));
}

}  // namespace

arrow::Result<std::unique_ptr<DimensionIndex>> buildDimensionIndex(
   const ResolvedGroupingDimension& dimension,
   const storage::Table& table,
   const Bitmap& filter_bitmap
) {
   if (const auto* sequence_position = std::get_if<ResolvedSequencePositionDimension>(&dimension)) {
      if (sequence_position->is_nucleotide) {
         return buildSequencePositionIndex<Nucleotide>(table, *sequence_position);
      }
      return buildSequencePositionIndex<AminoAcid>(table, *sequence_position);
   }
   if (const auto* indexed_column = std::get_if<ResolvedIndexedColumnDimension>(&dimension)) {
      const auto& column =
         table.getColumn<storage::column::DictionaryEncodedColumn>(indexed_column->column_name);
      return std::make_unique<IndexedColumnIndex>(column);
   }
   const auto& scalar_expression = std::get<ResolvedScalarExpressionDimension>(dimension);
   switch (scalar_expression.output_type) {
      case schema::ColumnType::STRING:
      case schema::ColumnType::DICTIONARY_ENCODED:
         return buildScalarExpressionIndex<StringValueTraits>(
            table, scalar_expression, filter_bitmap
         );
      case schema::ColumnType::INT32:
         return buildScalarExpressionIndex<Int32ValueTraits>(
            table, scalar_expression, filter_bitmap
         );
      case schema::ColumnType::INT64:
         return buildScalarExpressionIndex<Int64ValueTraits>(
            table, scalar_expression, filter_bitmap
         );
      case schema::ColumnType::FLOAT:
         return buildScalarExpressionIndex<DoubleValueTraits>(
            table, scalar_expression, filter_bitmap
         );
      case schema::ColumnType::BOOL:
         return buildScalarExpressionIndex<BoolValueTraits>(
            table, scalar_expression, filter_bitmap
         );
      case schema::ColumnType::DATE32:
         return buildScalarExpressionIndex<Date32ValueTraits>(
            table, scalar_expression, filter_bitmap
         );
      default:
         // The rewrite pass only routes groupable scalar output types here; anything else is a bug.
         return arrow::Status::Invalid(
            "bitmap aggregation cannot group on the expression's output type"
         );
   }
}

}  // namespace rhydb::query_engine::exec_node
