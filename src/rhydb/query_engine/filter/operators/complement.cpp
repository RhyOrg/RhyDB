#include "rhydb/query_engine/filter/operators/complement.h"

#include <string>
#include <utility>

#include <roaring/roaring.hh>

#include "evobench/evobench.hpp"
#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/intersection.h"
#include "rhydb/query_engine/filter/operators/operator.h"

namespace rhydb::query_engine::filter::operators {

Complement::Complement(std::unique_ptr<Operator> child, storage::column::RowLayout row_layout)
    : Operator(std::move(row_layout)),
      child(std::move(child)) {}

Complement::~Complement() noexcept = default;

using operators::OperatorVector;

std::unique_ptr<Complement> Complement::fromDeMorgan(
   OperatorVector disjunction,
   storage::column::RowLayout row_layout
) {
   OperatorVector non_negated_child_operators;
   OperatorVector negated_child_operators;
   for (auto& disjunction_child : disjunction) {
      if (disjunction_child->type() == operators::COMPLEMENT) {
         negated_child_operators.emplace_back(Operator::negate(std::move(disjunction_child)));
      } else {
         non_negated_child_operators.push_back(std::move(disjunction_child));
      }
   }
   // Now swap negated children and non-negated ones
   auto intersection = std::make_unique<operators::Intersection>(
      std::move(negated_child_operators), std::move(non_negated_child_operators), row_layout
   );
   return std::make_unique<Complement>(std::move(intersection), std::move(row_layout));
}

std::string Complement::toString() const {
   return "!" + child->toString();
}

Type Complement::type() const {
   return COMPLEMENT;
}

roaring_util::CopyOnWriteContainer Complement::evaluateChunk(uint16_t chunk_id) const {
   EVOBENCH_SCOPE("Complement", "evaluateChunk");
   return roaring_util::CopyOnWriteContainer{roaring_util::complement(
      child->evaluateChunk(chunk_id).view(), row_layout.chunkSize(chunk_id)
   )};
}

std::unique_ptr<Operator> Complement::negate(std::unique_ptr<Complement>&& complement) {
   return std::move(complement->child);
}

}  // namespace rhydb::query_engine::filter::operators
