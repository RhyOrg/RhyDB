#include "rhydb/query_engine/operators/bitmap_aggregation_node.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/array.h>
#include <arrow/array/util.h>
#include <arrow/builder.h>
#include <arrow/compute/api.h>
#include <arrow/compute/expression.h>
#include <arrow/datum.h>
#include <arrow/result.h>
#include <nlohmann/json.hpp>
#include <roaring/roaring.hh>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/bitmap.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/common/panic.h"
#include "rhydb/common/symbol_map.h"
#include "rhydb/query_engine/exec_node/arrow_util.h"
#include "rhydb/query_engine/exec_node/table_scan.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/operators/compute_filter.h"
#include "rhydb/query_engine/scalar_expressions/scalar_expression.h"
#include "rhydb/roaring_util/roaring_container.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/row_id.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/table.h"

namespace rhydb::query_engine::operators {

namespace {

using roaring_util::CopyOnWriteContainer;
using roaring_util::RoaringContainer;
using roaring_util::RoaringContainerView;

/// One observed combination across the grouping dimensions together with the number of (filtered)
/// rows carrying it. Each entry of `group_indices` indexes into the corresponding dimension's
/// group labels, i.e. it identifies which group was chosen in that dimension. Keeping indices
/// rather than the values themselves makes the recursion type-agnostic: the actual value is
/// resolved per dimension only when the output is materialized.
struct GroupCombination {
   std::vector<size_t> group_indices;
   uint64_t count;
};

/// Hashes a group-index tuple so combination counts can accumulate in an `unordered_map`
struct GroupIndicesHash {
   size_t operator()(const std::vector<size_t>& indices) const {
      size_t seed = indices.size();
      for (const size_t index : indices) {
         // boost-style hash_combine
         seed ^= index + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
      }
      return seed;
   }
};

/// In a chunk, there is either only a single key present (SingletonKeyGroup), or many keys are
/// present (KeyGroupList). If only one group is present, we need to only store the key index,
/// otherwise we store the list of key indexes and corresponding bitmaps indicating which rows
/// belong to that group
using SingletonKeyGroup = size_t;
using KeyGroupList = std::vector<std::pair<size_t, CopyOnWriteContainer>>;

using KeyGroupsInChunk = std::variant<KeyGroupList, SingletonKeyGroup>;

/// Individual key groups are combined and then the count per group stored in this data structure
using CombinationCounts = std::unordered_map<std::vector<size_t>, uint64_t, GroupIndicesHash>;

/// For every chunk, the resolved groups for this group-by key are stored
class KeyGroups {
  public:
   KeyGroups() = default;
   KeyGroups(const KeyGroups&) = delete;
   KeyGroups& operator=(const KeyGroups&) = delete;
   KeyGroups(KeyGroups&&) = delete;
   KeyGroups& operator=(KeyGroups&&) = delete;
   virtual ~KeyGroups() = default;

   // Returns the groups for this key after filtering
   [[nodiscard]] virtual KeyGroupsInChunk keyGroups(
      uint16_t chunk_id,
      RoaringContainerView filter_view
   ) const = 0;

   /// All values that this group key can have
   [[nodiscard]] virtual arrow::Result<std::shared_ptr<arrow::Array>> keyValues() const = 0;
};

/// Groups the rows by the symbol they carry at a fixed sequence position
/// Introduced as a speed-up when `<seq>.at(<position>)` was detected as group key expression
template <typename SymbolType>
class SequencePositionGrouper : public KeyGroups {
   static constexpr size_t SYMBOL_COUNT = SymbolType::SYMBOLS.size();
   // The null group's index sits just past every symbol
   static constexpr size_t NULL_INDEX = SYMBOL_COUNT;

   const storage::column::SequenceColumn<SymbolType>& column;
   uint32_t position_idx;
   typename SymbolType::Symbol reference_symbol;
   typename SymbolType::Symbol missing_symbol = SymbolType::SYMBOL_MISSING;

   // The position's vertical mutation containers, bucketed by 2^16 chunk so the per-chunk work is a
   // lookup rather than a rescan of the position's whole diff range. Views into the stored index.
   std::map<uint16_t, std::vector<std::pair<typename SymbolType::Symbol, RoaringContainerView>>>
      mutations_by_chunk;
   // The column's null rows, one view per 2^16 chunk (into `column.null_bitmap`).
   std::map<uint16_t, RoaringContainerView> null_views;

  public:
   SequencePositionGrouper(
      const storage::column::SequenceColumn<SymbolType>& column,
      uint32_t position_idx
   )
       : column(column),
         position_idx(position_idx) {
      CHECK_RHYDB_QUERY(
         position_idx < column.metadata->reference_sequence.size(),
         "{}.at({}) is out of bounds: the {} sequence has length {}",
         column.metadata->column_name,
         position_idx + 1,
         SymbolType::SYMBOL_NAME_LOWER_CASE,
         column.metadata->reference_sequence.size()
      );
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

   [[nodiscard]] KeyGroupsInChunk keyGroups(uint16_t chunk_id, RoaringContainerView filter_view)
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

      KeyGroupList groups;

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
   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Array>> keyValues() const override {
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
class IndexedColumnGrouper : public KeyGroups {
   // chunk key -> the groups holding a container in that chunk. Precomputed once (the inverted
   // index is unordered), so per-chunk grouping is a single map lookup returning views into stored
   // bitmaps.
   std::map<uint16_t, KeyGroupList> groups_by_chunk;
   // The distinct value of each group index, in sorted order (the null group has no entry; it is
   // the trailing null appended by `keyValues`).
   std::vector<std::string> key_values;

  public:
   explicit IndexedColumnGrouper(const storage::column::DictionaryEncodedColumn& column) {
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

   [[nodiscard]] KeyGroupsInChunk keyGroups(uint16_t chunk_id, RoaringContainerView /*filter_view*/)
      const override {
      if (auto iter = groups_by_chunk.find(chunk_id); iter != groups_by_chunk.end()) {
         return iter->second;
      }
      return KeyGroupList{};
   }

   /// The distinct values in sorted order (the group indices `keyGroups` hands out), then a null
   /// for the trailing null group.
   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Array>> keyValues() const override {
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

std::unique_ptr<KeyGroups> makeGrouper(
   const SequencePositionDimension& dimension,
   const storage::Table& table,
   const Bitmap& /*filter_bitmap*/
) {
   if (dimension.is_nucleotide) {
      const auto& column = table.columns.getColumns<Nucleotide::Column>().at(dimension.column.name);
      return std::make_unique<SequencePositionGrouper<Nucleotide>>(column, dimension.position_idx);
   }
   const auto& column = table.columns.getColumns<AminoAcid::Column>().at(dimension.column.name);
   return std::make_unique<SequencePositionGrouper<AminoAcid>>(column, dimension.position_idx);
}

std::unique_ptr<KeyGroups> makeGrouper(
   const IndexedColumnDimension& dimension,
   const storage::Table& table,
   const Bitmap& /*filter_bitmap*/
) {
   const auto& column =
      table.columns.getColumns<storage::column::DictionaryEncodedColumn>().at(dimension.column.name
      );
   return std::make_unique<IndexedColumnGrouper>(column);
}

// The grouper for a map-produced scalar expression evaluates it via Arrow (there is no column to
// read straight off), so its work goes through arrow::Result. These raise those failures as query
// errors, since `makeGrouper` -- like the other dimensions' -- returns a plain grouper and reports
// problems by throwing.
template <typename T>
T orThrowQuery(arrow::Result<T> result) {
   CHECK_RHYDB_QUERY(result.ok(), "{}", result.status().ToString());
   return std::move(result).ValueOrDie();
}
void orThrowQuery(const arrow::Status& status) {
   CHECK_RHYDB_QUERY(status.ok(), "{}", status.ToString());
}

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

/// The distinct columns `expression` reads, resolved against the table so each carries its real
/// column type (needed to materialize it and bind the expression).
std::vector<schema::ColumnIdentifier> resolveReferencedColumns(
   const scalar_expressions::ScalarExpression& expression,
   const storage::Table& table
) {
   std::vector<schema::ColumnIdentifier> referenced;
   std::unordered_set<std::string> seen;
   for (const auto& column : expression.freeIUs()) {
      if (seen.insert(column.name).second) {
         const auto resolved = table.schema->getColumn(column.name);
         CHECK_RHYDB_QUERY(
            resolved.has_value(), "bitmap aggregation references unknown column '{}'", column.name
         );
         referenced.push_back(resolved.value());
      }
   }
   return referenced;
}

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

   exec_node::ExecBatchBuilder batch_builder{referenced};
   ARROW_RETURN_NOT_OK(
      batch_builder.appendEntries(table, Bitmap::fromContainerViews({{chunk_id, chunk_rows}}))
   );
   ARROW_ASSIGN_OR_RAISE(auto batch, batch_builder.finishBatch());

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

/// The per-value bitmaps, per-chunk group views and typed value array a `ScalarExpressionGrouper`
/// serves, built once up front.
struct ScalarGroupData {
   // Owned per-value bitmaps (sorted values, then the null group), so the views below never dangle.
   std::vector<roaring::Roaring> value_bitmaps;
   std::map<uint16_t, KeyGroupList> groups_by_chunk;
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
ScalarGroupData buildScalarGroups(
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
      const std::shared_ptr<arrow::Array> array = orThrowQuery(evaluateExpressionForRows(
         table, referenced, bound_expression, output_type, chunk_id, filter_view, exec_context
      ));
      const auto& typed = static_cast<const typename Traits::ArrayType&>(*array);
      CHECK_RHYDB_QUERY(
         typed.length() == filter_view.getCardinality(),
         "scalar expression produced {} values for {} rows",
         typed.length(),
         filter_view.getCardinality()
      );
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
      orThrowQuery(Traits::append(value_builder, value));
      for (const auto& [chunk_id, view] : Bitmap{&data.value_bitmaps.back()}) {
         data.groups_by_chunk[chunk_id].emplace_back(group_index, CopyOnWriteContainer{view});
      }
   }
   const size_t null_index = data.value_bitmaps.size();
   data.value_bitmaps.push_back(std::move(null_bitmap));
   orThrowQuery(value_builder.AppendNull());
   for (const auto& [chunk_id, view] : Bitmap{&data.value_bitmaps.back()}) {
      data.groups_by_chunk[chunk_id].emplace_back(null_index, CopyOnWriteContainer{view});
   }
   data.group_value_array = orThrowQuery(value_builder.Finish());
   return data;
}

/// Groups rows by the value of a map-produced scalar expression (e.g. `map({week :=
/// date.isoWeek()})`). Everything is precomputed in `buildScalarGroups` -- over the filtered rows
/// only, so neither the bitmaps nor the distinct values cover more than the query touches;
/// per-chunk grouping is a single map lookup, and `keyValues` returns the typed value array so the
/// output column keeps the expression's type.
class ScalarExpressionGrouper : public KeyGroups {
   ScalarGroupData data;

  public:
   explicit ScalarExpressionGrouper(ScalarGroupData data)
       : data(std::move(data)) {}

   [[nodiscard]] KeyGroupsInChunk keyGroups(uint16_t chunk_id, RoaringContainerView /*filter_view*/)
      const override {
      if (auto iter = data.groups_by_chunk.find(chunk_id); iter != data.groups_by_chunk.end()) {
         return iter->second;
      }
      return KeyGroupList{};
   }

   [[nodiscard]] arrow::Result<std::shared_ptr<arrow::Array>> keyValues() const override {
      return data.group_value_array;
   }
};

std::unique_ptr<KeyGroups> makeGrouper(
   const ScalarExpressionDimension& dimension,
   const storage::Table& table,
   const Bitmap& filter_bitmap
) {
   const auto referenced = resolveReferencedColumns(*dimension.expression, table);
   const auto arrow_expression = orThrowQuery(dimension.expression->toArrowExpression());
   const auto input_schema = exec_node::columnsToArrowSchema(referenced);
   const auto bound_expression = orThrowQuery(arrow_expression.Bind(*input_schema));
   const auto output_type = exec_node::columnTypeToArrowType(dimension.output_type);

   ScalarGroupData data;
   switch (dimension.output_type) {
      case schema::ColumnType::STRING:
      case schema::ColumnType::DICTIONARY_ENCODED:
         data = buildScalarGroups<StringValueTraits>(
            table, referenced, bound_expression, output_type, filter_bitmap
         );
         break;
      case schema::ColumnType::INT32:
         data = buildScalarGroups<Int32ValueTraits>(
            table, referenced, bound_expression, output_type, filter_bitmap
         );
         break;
      case schema::ColumnType::INT64:
         data = buildScalarGroups<Int64ValueTraits>(
            table, referenced, bound_expression, output_type, filter_bitmap
         );
         break;
      case schema::ColumnType::FLOAT:
         data = buildScalarGroups<DoubleValueTraits>(
            table, referenced, bound_expression, output_type, filter_bitmap
         );
         break;
      case schema::ColumnType::BOOL:
         data = buildScalarGroups<BoolValueTraits>(
            table, referenced, bound_expression, output_type, filter_bitmap
         );
         break;
      case schema::ColumnType::DATE32:
         data = buildScalarGroups<Date32ValueTraits>(
            table, referenced, bound_expression, output_type, filter_bitmap
         );
         break;
      default:
         // The rewrite pass only routes groupable scalar output types here; anything else is a bug.
         throw IllegalQueryException(
            "bitmap aggregation cannot group on the expression's output type"
         );
   }
   return std::make_unique<ScalarExpressionGrouper>(std::move(data));
}

/// Recursively intersect one chunk's per-dimension group containers, depth by depth, and add the
/// cardinality of each surviving full combination to `counts`. `current` is the running
/// container-level intersection of the groups chosen so far; it is seeded with the filter chunk (so
/// the not-yet-filtered group views are bounded by the filter here) and is only ever a view or a
/// short-lived owned temporary, so no `Bitmap` is built and no group container is
/// copied. Empty intersections are pruned. A dimension whose groups collapsed to a single
/// whole-chunk label contributes no intersection at all -- the running set passes through unchanged
/// with that label fixed.
// NOLINTNEXTLINE(misc-no-recursion)
void aggregateChunk(
   size_t depth,
   const roaring::internal::container_t* current,
   uint8_t current_typecode,
   const std::vector<const KeyGroupsInChunk*>& groups_by_dimension,
   std::vector<size_t>& chosen_indices,
   CombinationCounts& counts
) {
   const size_t last_dimension = groups_by_dimension.size() - 1;

   if (const size_t* group_index = std::get_if<SingletonKeyGroup>(groups_by_dimension[depth])) {
      // Every row of the running intersection carries this one label; nothing to intersect.
      chosen_indices[depth] = *group_index;
      if (depth == last_dimension) {
         const auto count = static_cast<uint64_t>(
            roaring::internal::container_get_cardinality(current, current_typecode)
         );
         if (count > 0) {
            counts[chosen_indices] += count;
         }
      } else {
         aggregateChunk(
            depth + 1, current, current_typecode, groups_by_dimension, chosen_indices, counts
         );
      }
      return;
   }

   for (const auto& [group_index, group] : std::get<KeyGroupList>(*groups_by_dimension[depth])) {
      chosen_indices[depth] = group_index;
      const RoaringContainerView view = group.view();

      if (depth == last_dimension) {
         // Leaf: only the cardinality of the final intersection is needed, so compute it directly
         // without allocating a result container.
         const auto count = static_cast<uint64_t>(roaring::internal::container_and_cardinality(
            current, current_typecode, view.rawContainer(), view.getTypecode()
         ));
         if (count > 0) {
            counts[chosen_indices] += count;
         }
         continue;
      }

      uint8_t result_typecode = 0;
      auto* intersection = roaring::internal::container_and(
         current, current_typecode, view.rawContainer(), view.getTypecode(), &result_typecode
      );
      if (roaring::internal::container_nonzero_cardinality(intersection, result_typecode)) {
         aggregateChunk(
            depth + 1, intersection, result_typecode, groups_by_dimension, chosen_indices, counts
         );
      }
      roaring::internal::container_free(intersection, result_typecode);
   }
}

/// Computes the aggregation groups counts one 2^16 chunk at a time. It does so by enumerating all
/// combination of the per-key groups. The combination's cardinalities are computed efficiently
/// using bitmap intersection
std::vector<GroupCombination> computeCombinations(
   const std::vector<std::unique_ptr<KeyGroups>>& groupers,
   const Bitmap& filter_bitmap
) {
   const size_t num_dimensions = groupers.size();
   if (num_dimensions == 0) {
      return {};
   }

   // Counts keyed by the group-index tuple, accumulated in an unordered_map for O(1) updates; the
   // result is sorted back into ascending (lexicographic-by-index) order below, which -- because
   // every dimension numbers its groups in output order -- is the order the result rows appear in.
   CombinationCounts counts;
   std::vector<size_t> chosen_indices(num_dimensions);

   for (const auto& [chunk_id, filter_view] : filter_bitmap) {
      // Build each dimension's groups for this chunk; each group carries its own rows (a
      // CopyOnWriteContainer that borrows stored bitmaps or owns computed ones), so nothing
      // external needs to outlive the aggregation. A filter container is never empty, and each
      // dimension's groups partition its rows, so every dimension yields at least one group; a
      // dimension that (defensively) produced none would simply contribute no combinations in
      // `aggregateChunk`.
      std::vector<KeyGroupsInChunk> groups(num_dimensions);
      std::vector<const KeyGroupsInChunk*> groups_at_chunk(num_dimensions);
      for (size_t dimension = 0; dimension < num_dimensions; ++dimension) {
         groups[dimension] = groupers[dimension]->keyGroups(chunk_id, filter_view);
         groups_at_chunk[dimension] = &groups[dimension];
      }
      aggregateChunk(
         0,
         filter_view.rawContainer(),
         filter_view.getTypecode(),
         groups_at_chunk,
         chosen_indices,
         counts
      );
   }

   std::vector<GroupCombination> combinations;
   combinations.reserve(counts.size());
   while (!counts.empty()) {
      auto node = counts.extract(counts.begin());
      combinations.push_back(
         GroupCombination{.group_indices = std::move(node.key()), .count = node.mapped()}
      );
   }
   // Restore the ascending group-index-tuple order the unordered_map does not keep.
   std::ranges::sort(combinations, std::less{}, &GroupCombination::group_indices);
   return combinations;
}

/// Materializes one chunk of this operator's output: the group keys and aggregation output (count)
arrow::Result<arrow::ExecBatch> buildBatch(
   const std::vector<GroupCombination>& combinations,
   const std::vector<std::shared_ptr<arrow::Array>>& values_per_dimension,
   size_t dimension_count,
   size_t begin,
   size_t end
) {
   std::vector<arrow::Datum> result_columns;
   result_columns.reserve(dimension_count + 1);

   for (size_t i = 0; i < dimension_count; ++i) {
      // The group indices of this dimension for the combinations in [begin, end), as an int32 array
      // to Take the value array with. Distinct group counts fit comfortably in int32.
      arrow::Int32Builder index_builder;
      ARROW_RETURN_NOT_OK(index_builder.Reserve(static_cast<int64_t>(end - begin)));
      for (size_t combination_idx = begin; combination_idx < end; ++combination_idx) {
         index_builder.UnsafeAppend(
            static_cast<int32_t>(combinations[combination_idx].group_indices[i])
         );
      }
      std::shared_ptr<arrow::Array> indices;
      ARROW_RETURN_NOT_OK(index_builder.Finish(&indices));
      arrow::Datum gathered;
      ARROW_ASSIGN_OR_RAISE(gathered, arrow::compute::Take(values_per_dimension[i], indices));
      result_columns.push_back(std::move(gathered));
   }

   arrow::Int64Builder count_builder;
   ARROW_RETURN_NOT_OK(count_builder.Reserve(static_cast<int64_t>(end - begin)));
   for (size_t combination_idx = begin; combination_idx < end; ++combination_idx) {
      count_builder.UnsafeAppend(static_cast<int64_t>(combinations[combination_idx].count));
   }
   arrow::Datum count_datum;
   ARROW_ASSIGN_OR_RAISE(count_datum, count_builder.Finish());
   result_columns.push_back(std::move(count_datum));

   return arrow::ExecBatch::Make(result_columns);
}

}  // namespace

SequencePositionDimension::SequencePositionDimension(
   schema::ColumnIdentifier column,
   uint32_t position_idx,
   bool is_nucleotide,
   std::string output_name
)
    : column(std::move(column)),
      position_idx(position_idx),
      is_nucleotide(is_nucleotide),
      output_name(std::move(output_name)) {}

schema::ColumnIdentifier SequencePositionDimension::outputColumn() const {
   return {.name = output_name, .type = schema::ColumnType::STRING};
}

nlohmann::json SequencePositionDimension::toJson() const {
   return {
      {"kind", "sequencePosition"},
      {"column", columnToJson(column)},
      {"position", position_idx},
      {"isNucleotide", is_nucleotide},
      {"outputName", output_name},
   };
}

IndexedColumnDimension::IndexedColumnDimension(
   schema::ColumnIdentifier column,
   std::string output_name
)
    : column(std::move(column)),
      output_name(std::move(output_name)) {}

schema::ColumnIdentifier IndexedColumnDimension::outputColumn() const {
   return {.name = output_name, .type = schema::ColumnType::STRING};
}

nlohmann::json IndexedColumnDimension::toJson() const {
   return {
      {"kind", "indexedColumn"},
      {"column", columnToJson(column)},
      {"outputName", output_name},
   };
}

ScalarExpressionDimension::ScalarExpressionDimension(
   std::unique_ptr<scalar_expressions::ScalarExpression> expression,
   schema::ColumnType output_type,
   std::string output_name
)
    : expression(std::move(expression)),
      output_type(output_type),
      output_name(std::move(output_name)) {}

schema::ColumnIdentifier ScalarExpressionDimension::outputColumn() const {
   return {.name = output_name, .type = output_type};
}

nlohmann::json ScalarExpressionDimension::toJson() const {
   return {
      {"kind", "scalarExpression"},
      {"expression", expression->toString()},
      {"outputType", std::string{schema::columnTypeToString(output_type)}},
      {"outputName", output_name},
   };
}

BitmapAggregationNode::BitmapAggregationNode(
   std::shared_ptr<storage::Table> table,
   std::unique_ptr<scalar_expressions::ScalarExpression> filter,
   std::vector<GroupingDimension> dimensions,
   std::string count_field_name
)
    : table(std::move(table)),
      filter(std::move(filter)),
      dimensions(std::move(dimensions)),
      count_field_name(std::move(count_field_name)) {}

std::vector<schema::ColumnIdentifier> BitmapAggregationNode::getOutputSchema() const {
   std::vector<schema::ColumnIdentifier> output_fields;
   output_fields.reserve(dimensions.size() + 1);
   for (const auto& dimension : dimensions) {
      output_fields.emplace_back(
         std::visit([](const auto& dim) { return dim.outputColumn(); }, dimension)
      );
   }
   output_fields.emplace_back(count_field_name, schema::ColumnType::INT64);
   return output_fields;
}

nlohmann::json BitmapAggregationNode::toJson() const {
   nlohmann::json dimensions_json = nlohmann::json::array();
   for (const auto& dimension : dimensions) {
      dimensions_json.push_back(std::visit([](const auto& dim) { return dim.toJson(); }, dimension)
      );
   }
   return {
      {"type", nodeKindToString(kind())},
      {"filter", filter->toString()},
      {"dimensions", std::move(dimensions_json)},
      {"countFieldName", count_field_name},
   };
}

arrow::Result<arrow::acero::ExecNode*> BitmapAggregationNode::addToExecPlan(
   arrow::acero::ExecPlan& plan,
   const std::map<schema::TableName, std::shared_ptr<storage::Table>>& /*tables*/,
   const config::QueryOptions& query_options
) const {
   auto filter_bitmap = computeFilter(filter, *table);

   // Resolve each dimension against the table into a grouper that produces its groups per 2^16
   // chunk (this also validates, e.g. a sequence position out of range throws here). The groups are
   // then built and counted chunk by chunk, never materializing whole-table per-group bitmaps.
   std::vector<std::unique_ptr<KeyGroups>> groupers;
   groupers.reserve(dimensions.size());
   for (const auto& dimension : dimensions) {
      groupers.push_back(std::visit(
         [&](const auto& dim) { return makeGrouper(dim, *table, filter_bitmap); }, dimension
      ));
   }

   std::vector<GroupCombination> combinations = computeCombinations(groupers, filter_bitmap);

   const size_t dimension_count = dimensions.size();

   // The group bitmaps are no longer needed once counting is done; keep only the per-dimension
   // value arrays the output materialization gathers the group indices from (element `group_index`
   // = that group's typed value).
   std::vector<std::shared_ptr<arrow::Array>> values_per_dimension;
   values_per_dimension.reserve(groupers.size());
   for (auto& grouper : groupers) {
      ARROW_ASSIGN_OR_RAISE(auto values, grouper->keyValues());
      values_per_dimension.push_back(std::move(values));
   }

   // Emit the combinations in pipeline-sized batches instead of a single unbounded one, and build
   // each batch only when the downstream pulls it rather than materializing the whole result up
   // front: the number of combinations is bounded only by the filtered row count, so a
   // many-dimension query can produce a very large result and holding it all at once would blow up
   // peak memory. `materialization_cutoff` is the batch-size-minus-one knob the rest of the
   // pipeline (e.g. the table scan) uses, so this output is sized the same way.
   const size_t batch_size = query_options.materialization_cutoff + 1;

   std::function<arrow::Future<std::optional<arrow::ExecBatch>>()> producer =
      [combinations = std::move(combinations),
       values_per_dimension = std::move(values_per_dimension),
       dimension_count,
       batch_size,
       begin = size_t{0}]() mutable -> arrow::Future<std::optional<arrow::ExecBatch>> {
      if (begin >= combinations.size()) {
         return arrow::Future<std::optional<arrow::ExecBatch>>::MakeFinished(std::nullopt);
      }
      const size_t end = std::min(begin + batch_size, combinations.size());
      arrow::Result<arrow::ExecBatch> batch =
         buildBatch(combinations, values_per_dimension, dimension_count, begin, end);
      begin = end;
      return arrow::Future<std::optional<arrow::ExecBatch>>::MakeFinished(batch.Map(
         [](arrow::ExecBatch value) { return std::optional<arrow::ExecBatch>{std::move(value)}; }
      ));
   };

   const arrow::acero::SourceNodeOptions options{
      exec_node::columnsToArrowSchema(getOutputSchema()),
      std::move(producer),
      arrow::Ordering::Implicit(),
   };
   return arrow::acero::MakeExecNode("source", &plan, {}, options);
}

}  // namespace rhydb::query_engine::operators
