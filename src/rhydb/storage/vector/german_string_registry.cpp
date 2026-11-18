#include "rhydb/storage/vector/german_string_registry.h"

namespace rhydb::storage::vector {

Idx GermanStringRegistry::insert(const RhyDBString& rhydb_string) {
   const bool need_new_page = german_string_pages.empty() || german_string_pages.back().full();
   if (need_new_page) {
      german_string_pages.emplace_back();
   }
   const size_t page_id = german_string_pages.size() - 1;
   const size_t row_in_page = german_string_pages.back().insert(rhydb_string);
   return (page_id * GermanStringPage::MAX_STRINGS_PER_PAGE) + row_in_page;
}

RhyDBString GermanStringRegistry::get(Idx row_id) const {
   const Idx page_id = row_id / GermanStringPage::MAX_STRINGS_PER_PAGE;
   const Idx row_in_page = row_id - (page_id * GermanStringPage::MAX_STRINGS_PER_PAGE);
   return german_string_pages.at(page_id).get(row_in_page);
}

void GermanStringRegistry::set(Idx row_id, const RhyDBString& rhydb_string) {
   const Idx page_id = row_id / GermanStringPage::MAX_STRINGS_PER_PAGE;
   const Idx row_in_page = row_id - (page_id * GermanStringPage::MAX_STRINGS_PER_PAGE);
   german_string_pages.at(page_id).set(row_in_page, rhydb_string);
}

void GermanStringRegistry::truncate(size_t num_values) {
   RHYDB_ASSERT(num_values <= numValues());
   const size_t num_pages = (num_values + GermanStringPage::MAX_STRINGS_PER_PAGE - 1) /
                            GermanStringPage::MAX_STRINGS_PER_PAGE;
   while (german_string_pages.size() > num_pages) {
      german_string_pages.pop_back();
   }
   if (num_pages > 0) {
      german_string_pages.back().truncate(
         static_cast<uint16_t>(
            num_values - ((num_pages - 1) * GermanStringPage::MAX_STRINGS_PER_PAGE)
         )
      );
   }
}

}  // namespace rhydb::storage::vector