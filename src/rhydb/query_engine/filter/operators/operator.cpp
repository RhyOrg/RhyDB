#include "rhydb/query_engine/filter/operators/operator.h"

#include <cstdlib>
#include <utility>

#include <evobench/evobench.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/filter/operators/bitmap_producer.h"
#include "rhydb/query_engine/filter/operators/complement.h"
#include "rhydb/query_engine/filter/operators/empty.h"
#include "rhydb/query_engine/filter/operators/full.h"
#include "rhydb/query_engine/filter/operators/index_scan.h"
#include "rhydb/query_engine/filter/operators/intersection.h"
#include "rhydb/query_engine/filter/operators/range_selection.h"
#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/query_engine/filter/operators/threshold.h"
#include "rhydb/query_engine/filter/operators/union.h"

namespace rhydb::query_engine::filter::operators {

Operator::Operator(storage::column::RowLayout row_layout)
    : row_layout(std::move(row_layout)) {}

Operator::~Operator() noexcept = default;

Bitmap Operator::evaluate() const {
   EVOBENCH_SCOPE("Operator", "evaluate");
   Bitmap result;
   for (size_t chunk_id = 0; chunk_id < row_layout.numChunks(); ++chunk_id) {
      result.append(
         static_cast<uint16_t>(chunk_id), evaluateChunk(static_cast<uint16_t>(chunk_id))
      );
   }
   return result;
}

std::unique_ptr<Operator> Operator::negate(std::unique_ptr<Operator>&& some_operator) {
   switch (some_operator->type()) {
      case EMPTY: {
         auto* empty = dynamic_cast<Empty*>(some_operator.release());
         return Empty::negate(std::unique_ptr<Empty>(empty));
      }
      case FULL: {
         auto* full = dynamic_cast<Full*>(some_operator.release());
         return Full::negate(std::unique_ptr<Full>(full));
      }
      case INDEX_SCAN: {
         auto* index_scan = dynamic_cast<IndexScan*>(some_operator.release());
         return IndexScan::negate(std::unique_ptr<IndexScan>(index_scan));
      }
      case INTERSECTION: {
         auto* intersection = dynamic_cast<Intersection*>(some_operator.release());
         return Intersection::negate(std::unique_ptr<Intersection>(intersection));
      }
      case COMPLEMENT: {
         auto* complement = dynamic_cast<Complement*>(some_operator.release());
         return Complement::negate(std::unique_ptr<Complement>(complement));
      }
      case RANGE_SELECTION: {
         auto* range_selection = dynamic_cast<RangeSelection*>(some_operator.release());
         return RangeSelection::negate(std::unique_ptr<RangeSelection>(range_selection));
      }
      case SELECTION: {
         auto* selection = dynamic_cast<Selection*>(some_operator.release());
         return Selection::negate(std::unique_ptr<Selection>(selection));
      }
      case THRESHOLD: {
         auto* threshold = dynamic_cast<Threshold*>(some_operator.release());
         return Threshold::negate(std::unique_ptr<Threshold>(threshold));
      }
      case UNION: {
         auto* union_operator = dynamic_cast<Union*>(some_operator.release());
         return Union::negate(std::unique_ptr<Union>(union_operator));
      }
      case BITMAP_PRODUCER: {
         auto* bitmap_producer = dynamic_cast<BitmapProducer*>(some_operator.release());
         return BitmapProducer::negate(std::unique_ptr<BitmapProducer>(bitmap_producer));
      }
   }
   RHYDB_UNREACHABLE();
}

}  // namespace rhydb::query_engine::filter::operators
