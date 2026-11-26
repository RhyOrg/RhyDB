#include "rhydb/roaring_util/subset_ranks.h"

#include "rhydb/roaring_util/container_access.h"

namespace rhydb::roaring_util {

// Get the subset of A & B and compute their ranks with respect to A
// Example:    A     B     rank in A
//             3
//             4 --- 4 --> 2
//             5 --- 5 --> 3
//                   6
//             7
//             9 --- 9 --> 5
//
std::vector<uint64_t> roaringSubsetRanks(
   RoaringContainerView container_a,
   RoaringContainerView container_b,
   uint32_t base
) {
   const RoaringContainer a_and_b = container_a & container_b;
   if (a_and_b.empty()) {
      return {};
   }

   const size_t cardinality = a_and_b.getCardinality();
   std::vector<uint32_t> a_and_b_as_vector(cardinality);
   roaring::internal::container_to_uint32_array(
      a_and_b_as_vector.data(), detail::ContainerAccess::raw(a_and_b), a_and_b.getTypecode(), base
   );

   const detail::ContainerHeader header_a = detail::ContainerAccess::header(container_a);
   std::vector<uint64_t> ids_in_reconstructed_sequences(cardinality);
   roaring::internal::container_rank_many(
      header_a.get(),
      header_a.getTypecode(),
      base,
      a_and_b_as_vector.data(),
      a_and_b_as_vector.data() + cardinality,
      ids_in_reconstructed_sequences.data()
   );

   return ids_in_reconstructed_sequences;
}

}  // namespace rhydb::roaring_util
