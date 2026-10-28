#pragma once

#include <boost/serialization/deque.hpp>

#include "rhydb/common/german_string.h"
#include "rhydb/common/types.h"
#include "rhydb/storage/buffer/page.h"

namespace rhydb::storage::vector {

class GermanStringPage {
  public:
   // The 16KB buffer is structured as follows:
   //
   //     2B     14B          16B
   //    |---|----------|--------------|--------------|--------------|
   //    | n | reserved |  RhyDBString |  RhyDBString |  RhyDBString |
   //    |---|----------|--------------|--------------|--------------|
   //    |  RhyDBString |  RhyDBString |  RhyDBString |              |
   //    |--------------|--------------|--------------|              |
   //    |                                                           |
   //    |                                                           |
   //    |                            ...                            |
   //    |-----------------------------------------------------------|
   //                                                                `16384
   //
   // A total of 16384/16 - 1= 1023 RhyDBStrings would fit on the page
   static_assert(sizeof(RhyDBString) == 16);
   static constexpr size_t MAX_STRINGS_PER_PAGE = 1023;
   struct Header {
      std::array<uint8_t, 16> bytes;
   };

  private:
   buffer::Page page;

  public:
   GermanStringPage() { std::memset(page.buffer, 0, sizeof(Header)); }

   [[nodiscard]] uint16_t& n() const { return *reinterpret_cast<uint16_t*>(page.buffer); }

   [[nodiscard]] bool full() const { return n() == MAX_STRINGS_PER_PAGE; }

   [[nodiscard]] size_t insert(const RhyDBString& rhydb_string) const {
      RHYDB_ASSERT(full() == false);
      // We need to silence a false-positive warning, where the linter does not realise that
      // the placement-new in the next expression needs a writeable pointer
      // NOLINTNEXTLINE(misc-const-correctness)
      uint8_t* const start_of_next_string_struct =
         page.buffer + sizeof(Header) + (n() * sizeof(RhyDBString));
      new (start_of_next_string_struct) RhyDBString(rhydb_string);
      return n()++;
   }

   /// Overwrites the string at `row_id`, which must already be stored on the page.
   void set(const Idx& row_id, const RhyDBString& rhydb_string) const {
      RHYDB_ASSERT(row_id < n());
      // NOLINTNEXTLINE(misc-const-correctness)
      uint8_t* const start_of_string_struct =
         page.buffer + sizeof(Header) + (row_id * sizeof(RhyDBString));
      new (start_of_string_struct) RhyDBString(rhydb_string);
   }

   /// Keeps only the first `num_strings` strings of the page.
   void truncate(uint16_t num_strings) const {
      RHYDB_ASSERT(num_strings <= n());
      n() = num_strings;
   }

   [[nodiscard]] const RhyDBString& get(const Idx& row_id) const {
      RHYDB_ASSERT(row_id < n());
      uint8_t* start_of_string_struct =
         page.buffer + sizeof(Header) + (row_id * sizeof(RhyDBString));
      return *reinterpret_cast<RhyDBString*>(start_of_string_struct);
   }

  private:
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & page;
      // clang-format on
   }
};

class GermanStringRegistry {
   std::deque<GermanStringPage> german_string_pages;

  public:
   Idx insert(const RhyDBString& rhydb_string);

   [[nodiscard]] RhyDBString get(Idx row_id) const;

   /// Overwrites the string at `row_id`, which must already be stored.
   void set(Idx row_id, const RhyDBString& rhydb_string);

   /// Keeps only the first `num_values` strings, freeing the pages no longer needed.
   void truncate(size_t num_values);

   [[nodiscard]] size_t numValues() const {
      if (german_string_pages.empty()) {
         return 0;
      }
      const size_t values_on_closed_pages =
         (german_string_pages.size() - 1) * GermanStringPage::MAX_STRINGS_PER_PAGE;
      const size_t values_on_last_page = german_string_pages.back().n();
      return values_on_closed_pages + values_on_last_page;
   }

  private:
   friend class boost::serialization::access;

   template <class Archive>
   [[maybe_unused]] void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & german_string_pages;
      // clang-format on
   }
};

}  // namespace rhydb::storage::vector
