#include "rhydb/query_engine/optimizer/bitmap_aggregation_rewrite_pass.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/query_engine/operators/aggregate_node.h"
#include "rhydb/query_engine/operators/filter_node.h"
#include "rhydb/query_engine/operators/map_node.h"
#include "rhydb/query_engine/operators/query_node.h"
#include "rhydb/query_engine/operators/table_scan_node.h"
#include "rhydb/query_engine/scalar_expressions/at.h"
#include "rhydb/query_engine/scalar_expressions/field_ref.h"
#include "rhydb/query_engine/scalar_expressions/iso_week.h"
#include "rhydb/query_engine/scalar_expressions/literal.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column_metadata.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/table.h"

using rhydb::Nucleotide;
using rhydb::query_engine::optimizer::BitmapAggregationRewritePass;
using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
namespace scalar_expressions = rhydb::query_engine::scalar_expressions;
namespace operators = rhydb::query_engine::operators;

namespace {

const ColumnIdentifier NUC_COLUMN{.name = "nuc", .type = ColumnType::NUCLEOTIDE_SEQUENCE};
const ColumnIdentifier ID_COLUMN{.name = "id", .type = ColumnType::STRING};
const ColumnIdentifier DIVISION_COLUMN{.name = "division", .type = ColumnType::DICTIONARY_ENCODED};
const ColumnIdentifier HOST_COLUMN{.name = "host", .type = ColumnType::STRING};
const ColumnIdentifier DATE_COLUMN{.name = "date", .type = ColumnType::DATE32};
const ColumnIdentifier INT32_COLUMN{.name = "age", .type = ColumnType::INT32};
const ColumnIdentifier INT64_COLUMN{.name = "reads", .type = ColumnType::INT64};
const ColumnIdentifier FLOAT_COLUMN{.name = "coverage", .type = ColumnType::FLOAT};
const ColumnIdentifier BOOL_COLUMN{.name = "passed", .type = ColumnType::BOOL};

/// A table whose schema carries a nucleotide sequence column "nuc", an indexed string column
/// "division", a plain (non-indexed) string column "host", a "date" column, one column of each
/// remaining groupable scalar type (int32/int64/float/bool) and the "id" primary key, so the pass
/// can resolve every kind of grouping key against it. The columns hold no data:
/// the pass only reads the schema, it never executes the node.
std::shared_ptr<rhydb::storage::Table> tableWithColumns() {
   using rhydb::storage::column::ColumnMetadata;
   using rhydb::storage::column::DictionaryEncodedColumnMetadata;
   using rhydb::storage::column::SequenceColumnMetadata;
   using rhydb::storage::column::StringColumnMetadata;

   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> col_meta{
      {ID_COLUMN, std::make_shared<StringColumnMetadata>(ID_COLUMN.name)},
      {DIVISION_COLUMN, std::make_shared<DictionaryEncodedColumnMetadata>(DIVISION_COLUMN.name)},
      {HOST_COLUMN, std::make_shared<StringColumnMetadata>(HOST_COLUMN.name)},
      {DATE_COLUMN, std::make_shared<ColumnMetadata>(DATE_COLUMN.name)},
      {INT32_COLUMN, std::make_shared<ColumnMetadata>(INT32_COLUMN.name)},
      {INT64_COLUMN, std::make_shared<ColumnMetadata>(INT64_COLUMN.name)},
      {FLOAT_COLUMN, std::make_shared<ColumnMetadata>(FLOAT_COLUMN.name)},
      {BOOL_COLUMN, std::make_shared<ColumnMetadata>(BOOL_COLUMN.name)},
      {NUC_COLUMN,
       std::make_shared<SequenceColumnMetadata<Nucleotide>>(
          NUC_COLUMN.name, std::vector<Nucleotide::Symbol>{Nucleotide::Symbol::A}
       )}
   };
   auto schema = std::make_shared<rhydb::schema::TableSchema>(std::move(col_meta), ID_COLUMN);
   return std::make_shared<rhydb::storage::Table>(rhydb::schema::TableName::getDefault(), schema);
}

operators::QueryNodePtr makeScan() {
   return std::make_unique<operators::TableScanNode>(
      tableWithColumns(),
      std::make_unique<scalar_expressions::BoolLiteral>(true),
      std::vector<ColumnIdentifier>{}
   );
}

/// map({<field> := at(<column>, 1)}) over `child`.
operators::QueryNodePtr makeMapWithAt(
   operators::QueryNodePtr child,
   const std::string& field,
   const ColumnIdentifier& at_column
) {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = field, .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::At>(
          std::make_unique<scalar_expressions::FieldRef>(at_column), 1
       )}
   );
   return std::make_unique<operators::MapNode>(std::move(child), std::move(assignments));
}

/// map({<field> := <date_column>.isoWeek()}) over `child`.
operators::QueryNodePtr makeMapWithIsoWeek(
   operators::QueryNodePtr child,
   const std::string& field,
   const ColumnIdentifier& date_column
) {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = field, .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::IsoWeek>(
          std::make_unique<scalar_expressions::FieldRef>(date_column)
       )}
   );
   return std::make_unique<operators::MapNode>(std::move(child), std::move(assignments));
}

/// map({<field> := <source_column>}) over `child`: a bare field reference, no computation.
operators::QueryNodePtr makeMapWithFieldRef(
   operators::QueryNodePtr child,
   const std::string& field,
   const ColumnIdentifier& source_column,
   ColumnType output_type = ColumnType::STRING
) {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = {.name = field, .type = output_type},
       .expression = std::make_unique<scalar_expressions::FieldRef>(source_column)}
   );
   return std::make_unique<operators::MapNode>(std::move(child), std::move(assignments));
}

/// Adds `s := at(nuc, 1)` to `map` (which must be a MapNode), giving the grouping a bitmap-backed
/// sequence-position key next to whatever the map already computes.
operators::QueryNodePtr withSequencePosition(operators::QueryNodePtr map) {
   auto& map_node = dynamic_cast<operators::MapNode&>(*map);
   map_node.assignments.push_back(
      {.output_column = {.name = "s", .type = ColumnType::STRING},
       .expression = std::make_unique<scalar_expressions::At>(
          std::make_unique<scalar_expressions::FieldRef>(NUC_COLUMN), 1
       )}
   );
   return map;
}

/// map({<column> := <literal>}) over `child`: a user-defined map that overrides `column` in place,
/// standing in for any non-decompress map that could sit between the grouping map and the scan.
operators::QueryNodePtr makeMapOverridingColumn(
   operators::QueryNodePtr child,
   const ColumnIdentifier& column
) {
   std::vector<operators::MapNode::Assignment> assignments;
   assignments.push_back(
      {.output_column = column,
       .expression = std::make_unique<scalar_expressions::StringLiteral>("overridden")}
   );
   return std::make_unique<operators::MapNode>(std::move(child), std::move(assignments));
}

/// groupBy({count := count()}, {<fields>}) over `child`, with the single count aggregate optionally
/// carrying a source column (which takes it out of the recognized "bare count()" shape).
operators::QueryNodePtr makeGroupByCount(
   operators::QueryNodePtr child,
   const std::vector<std::string>& fields,
   std::optional<ColumnIdentifier> count_source = std::nullopt
) {
   std::vector<ColumnIdentifier> group_by;
   group_by.reserve(fields.size());
   for (const auto& field : fields) {
      group_by.push_back({.name = field, .type = ColumnType::STRING});
   }
   std::vector<operators::AggregateDefinition> aggregates{
      {.output_name = "count",
       .function = operators::AggregateFunction::COUNT,
       .source_column = std::move(count_source)}
   };
   return std::make_unique<operators::AggregateNode>(
      std::move(child), std::move(group_by), std::move(aggregates)
   );
}

// The canonical mutation co-occurrence shape: groupBy(count) over map(at(sequence column)) over a
// table scan is rewritten into the dedicated BitmapAggregationNode.
TEST(BitmapAggregationRewritePass, rewritesSequencePositionShape) {
   auto node = makeGroupByCount(makeMapWithAt(makeScan(), "s", NUC_COLUMN), {"s"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// groupBy(count) directly on an indexed string column (no map) is rewritten too: the column can be
// grouped straight from its inverted index.
TEST(BitmapAggregationRewritePass, rewritesIndexedColumnShape) {
   auto node = makeGroupByCount(makeScan(), {"division"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// A sequence position and an indexed column can be grouped together in one node.
TEST(BitmapAggregationRewritePass, rewritesMixedShape) {
   auto node = makeGroupByCount(makeMapWithAt(makeScan(), "s", NUC_COLUMN), {"s", "division"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// A bare field reference produced by the map over an indexed column (`r := division`) is rewritten:
// the column is grouped straight from its inverted index, just under a different output name.
TEST(BitmapAggregationRewritePass, rewritesMapFieldRefOverIndexedColumn) {
   auto node = makeGroupByCount(makeMapWithFieldRef(makeScan(), "r", DIVISION_COLUMN), {"r"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// A bare field reference produced by the map over a plain (non-indexed) string column (`h := host`)
// is rewritten alongside a sequence position: the grouper scans the column to build the per-value
// bitmaps itself.
TEST(BitmapAggregationRewritePass, rewritesMapFieldRefOverPlainStringColumn) {
   auto node = makeGroupByCount(
      withSequencePosition(makeMapWithFieldRef(makeScan(), "h", HOST_COLUMN)), {"s", "h"}
   );

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// A bare field reference over the nucleotide sequence column: the field-column matcher declines
// (it is not a string column) and the scalar-expression matcher also declines (a whole sequence
// column is not a groupable scalar), so the whole rewrite falls back to the generic pipeline.
TEST(BitmapAggregationRewritePass, declinesMapFieldRefOverNonStringColumn) {
   auto node = makeGroupByCount(makeMapWithFieldRef(makeScan(), "n", NUC_COLUMN), {"n"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// A general scalar expression the map computes -- here `date.isoWeek()` -- is grouped through the
// bitmap engine via the scalar-expression path when it sits next to a bitmap-backed key: the
// grouper evaluates it per row and buckets by the resulting value.
TEST(BitmapAggregationRewritePass, rewritesMapIsoWeekExpressionWithSequencePosition) {
   auto node = makeGroupByCount(
      withSequencePosition(makeMapWithIsoWeek(makeScan(), "week", DATE_COLUMN)), {"s", "week"}
   );

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// `at` on a non-sequence column (the STRING primary key) is not a sequence-position lookup, but it
// is still a general scalar expression the grouper can evaluate (it extracts a character), so next
// to an indexed column it is rewritten via the scalar-expression path rather than declined.
TEST(BitmapAggregationRewritePass, rewritesAtOnNonSequenceColumnWithIndexedColumn) {
   auto node = makeGroupByCount(makeMapWithAt(makeScan(), "p", ID_COLUMN), {"p", "division"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

// Grouping only on scalar expressions reuses no existing bitmap -- the grouper would just build one
// bitmap per distinct value, which for an alias of the primary key is one per row -- so the pass
// declines and leaves it to Arrow's streaming hash aggregation.
TEST(BitmapAggregationRewritePass, declinesWhenAllDimensionsAreScalarExpressions) {
   auto node = makeGroupByCount(makeMapWithFieldRef(makeScan(), "k", ID_COLUMN), {"k"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

TEST(BitmapAggregationRewritePass, declinesMapIsoWeekExpressionAlone) {
   auto node = makeGroupByCount(makeMapWithIsoWeek(makeScan(), "week", DATE_COLUMN), {"week"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

TEST(BitmapAggregationRewritePass, declinesAtOnNonSequenceColumnAlone) {
   auto node = makeGroupByCount(makeMapWithAt(makeScan(), "p", ID_COLUMN), {"p"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// Grouping directly on a non-indexed string column (the primary key) has no inverted index to read,
// so the pass declines and the generic pipeline handles it.
TEST(BitmapAggregationRewritePass, declinesWhenGroupingOnNonIndexedColumn) {
   auto node = makeGroupByCount(makeScan(), {"id"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// A count with a source column is not the bare count() the rewrite recognizes, so it declines.
TEST(BitmapAggregationRewritePass, declinesWhenAggregateIsNotBareCount) {
   auto node = makeGroupByCount(makeMapWithAt(makeScan(), "s", NUC_COLUMN), {"s"}, NUC_COLUMN);

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// --- bare count(*) with no grouping keys -> CountFilterNode ---

// A full count(*) directly over a scan is the filter's cardinality: replaced by a CountFilterNode
// that carries the query-assigned output name.
TEST(BitmapAggregationRewritePass, rewritesBareCountOverScanToCountFilter) {
   auto node = makeGroupByCount(makeScan(), {});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   ASSERT_EQ(result->kind(), operators::NodeKind::COUNT_FILTER);
   EXPECT_THAT(
      result->getOutputSchema(),
      ::testing::ElementsAre(ColumnIdentifier{.name = "count", .type = ColumnType::INT64})
   );
}

// The map that `default` inserts to decompress sequence columns is row-preserving and reads no
// column the count needs, so a count(*) over such a map still collapses to a CountFilterNode.
TEST(BitmapAggregationRewritePass, rewritesBareCountOverMapToCountFilter) {
   auto node = makeGroupByCount(makeMapWithFieldRef(makeScan(), "k", ID_COLUMN), {});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::COUNT_FILTER);
}

// A bare count(*) that does not sit on a (map over a) scan is left for the generic pipeline.
TEST(BitmapAggregationRewritePass, declinesBareCountOverNonScan) {
   auto filtered_scan = std::make_unique<operators::FilterNode>(
      makeScan(), std::make_unique<scalar_expressions::BoolLiteral>(true)
   );
   auto node = makeGroupByCount(std::move(filtered_scan), {});

   operators::QueryNodePtr result;
   ASSERT_NO_THROW(result = BitmapAggregationRewritePass::run(std::move(node)));
   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// count(<column>) without grouping keys is not the bare count(*) the rewrite handles: it must stay
// an AggregateNode so the generic path reports it as not-yet-implemented rather than the rewrite
// silently turning it into a count(*).
TEST(BitmapAggregationRewritePass, declinesBareCountWithSourceColumn) {
   auto node = makeGroupByCount(makeScan(), {}, ID_COLUMN);

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// When the matched pipeline does not sit directly on a table scan (here a FilterNode is in
// between), the pass must decline gracefully rather than throw: an optimizer may never turn a valid
// query into an error. The AggregateNode is left untouched for the generic pipeline to execute.
TEST(BitmapAggregationRewritePass, declinesWithoutThrowingWhenChildIsNotScan) {
   auto filtered_scan = std::make_unique<operators::FilterNode>(
      makeScan(), std::make_unique<scalar_expressions::BoolLiteral>(true)
   );
   auto node = makeGroupByCount(makeMapWithAt(std::move(filtered_scan), "s", NUC_COLUMN), {"s"});

   operators::QueryNodePtr result;
   ASSERT_NO_THROW(result = BitmapAggregationRewritePass::run(std::move(node)));
   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

// A non-decompress map (here a user-defined map overriding the "nuc" sequence column) between the
// grouping map and the table scan must not be skipped: it can change the very inputs the rewrite
// reads, so the pass declines rather than resolving the `At` against the raw table scan.
TEST(BitmapAggregationRewritePass, declinesWhenIntermediateMapIsNotDecompress) {
   auto overriding_map = makeMapOverridingColumn(makeScan(), NUC_COLUMN);
   auto node = makeGroupByCount(makeMapWithAt(std::move(overriding_map), "s", NUC_COLUMN), {"s"});

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::AGGREGATE);
}

}  // namespace

// Every groupable scalar type has its own ValueTraits instantiation in the bitmap aggregation node
// (key extraction, bucketing, value-array build). These pin down that a map field reference over a
// column of each type reaches that path at all (next to a sequence position, since scalar-only
// groupings are declined) -- without them, only the string and date traits were ever instantiated
// by a test.
TEST(BitmapAggregationRewritePass, rewritesMapFieldRefOverInt32Column) {
   auto node = makeGroupByCount(
      withSequencePosition(makeMapWithFieldRef(makeScan(), "a", INT32_COLUMN, ColumnType::INT32)),
      {"s", "a"}
   );

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

TEST(BitmapAggregationRewritePass, rewritesMapFieldRefOverInt64Column) {
   auto node = makeGroupByCount(
      withSequencePosition(makeMapWithFieldRef(makeScan(), "r", INT64_COLUMN, ColumnType::INT64)),
      {"s", "r"}
   );

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

TEST(BitmapAggregationRewritePass, rewritesMapFieldRefOverFloatColumn) {
   auto node = makeGroupByCount(
      withSequencePosition(makeMapWithFieldRef(makeScan(), "c", FLOAT_COLUMN, ColumnType::FLOAT)),
      {"s", "c"}
   );

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}

TEST(BitmapAggregationRewritePass, rewritesMapFieldRefOverBoolColumn) {
   auto node = makeGroupByCount(
      withSequencePosition(makeMapWithFieldRef(makeScan(), "p", BOOL_COLUMN, ColumnType::BOOL)),
      {"s", "p"}
   );

   auto result = BitmapAggregationRewritePass::run(std::move(node));

   EXPECT_EQ(result->kind(), operators::NodeKind::BITMAP_AGGREGATION);
}
