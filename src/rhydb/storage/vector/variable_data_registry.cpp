#include "rhydb/storage/vector/variable_data_registry.h"

#include "rhydb/common/panic.h"

namespace rhydb::storage::vector {

VariableDataRegistry::Identifier VariableDataRegistry::insert(std::string_view data) {
   if (variable_data_pages.empty() || offset + sizeof(size_t) > buffer::RHYDB_PAGE_SIZE) {
      variable_data_pages.emplace_back();
      offset = 0;
   }

   *reinterpret_cast<size_t*>(variable_data_pages.back().buffer + offset) = data.length();
   const size_t page_id = variable_data_pages.size() - 1;
   if (page_id > UINT32_MAX) {
      RHYDB_PANIC("Maximum number of variable string data reached. Aborting.");
   }
   VariableDataRegistry::Identifier identifier{
      .page_id = static_cast<uint32_t>(page_id), .offset = offset,
   };
   offset += sizeof(size_t);

   if (offset == buffer::RHYDB_PAGE_SIZE) {
      variable_data_pages.emplace_back();
      offset = 0;
   }

   std::string_view remaining_data = data;
   while (true) {
      const size_t space_for_next_data_piece = buffer::RHYDB_PAGE_SIZE - offset;
      RHYDB_ASSERT(space_for_next_data_piece > 0);
      if (space_for_next_data_piece >= remaining_data.length()) {
         std::memcpy(
            variable_data_pages.back().buffer + offset,
            remaining_data.data(),
            remaining_data.length()
         );
         offset += remaining_data.length();
         return identifier;
      }
      std::memcpy(
         variable_data_pages.back().buffer + offset,
         remaining_data.data(),
         space_for_next_data_piece
      );
      remaining_data = remaining_data.substr(space_for_next_data_piece);
      variable_data_pages.emplace_back();
      offset = 0;
   }
}

namespace {

VariableDataRegistry::DataList getDataFromPage(
   const buffer::Page& page,
   size_t offset,
   size_t length
) {
   const size_t length_on_page = std::min(length, buffer::RHYDB_PAGE_SIZE - offset);
   const char* start_pointer_on_page = reinterpret_cast<char*>(page.buffer + offset);
   const std::string_view data_on_page{start_pointer_on_page, length_on_page};
   return VariableDataRegistry::DataList{.data = data_on_page, .continuation = nullptr};
}

}  // namespace

VariableDataRegistry::DataList VariableDataRegistry::get(VariableDataRegistry::Identifier identifier
) const {
   auto length = *reinterpret_cast<size_t*>(
      variable_data_pages.at(identifier.page_id).buffer + identifier.offset
   );

   VariableDataRegistry::DataList ret = getDataFromPage(
      variable_data_pages.at(identifier.page_id), identifier.offset + sizeof(size_t), length
   );
   length -= ret.data.length();

   VariableDataRegistry::DataList* current = &ret;
   auto current_page = identifier.page_id + 1;
   while (length > 0) {
      // Continuation data is always at offset 0. Length is only stored on first page and then
      // directly spills to the beginning of next page. Append-only data-structure
      current->continuation = std::make_unique<VariableDataRegistry::DataList>(
         getDataFromPage(variable_data_pages.at(current_page), 0, length)
      );
      length -= current->continuation->data.length();
      current_page += 1;
      current = current->continuation.get();
   }
   return ret;
}

}  // namespace rhydb::storage::vector
