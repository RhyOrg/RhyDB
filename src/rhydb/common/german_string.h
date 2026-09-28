#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>

#include <boost/serialization/access.hpp>

#include "rhydb/common/panic.h"
#include "rhydb/storage/vector/variable_data_registry.h"

namespace rhydb {

/// Lexicographic comparison of two byte strings using unsigned byte ordering, so
/// bytes >= 0x80 (e.g. UTF-8 multibyte sequences) sort consistently regardless of
/// the platform's `char` signedness. Matches std::string_view / memcmp semantics,
/// keeping all string comparison paths (short, long, dictionary) in agreement.
inline std::strong_ordering compareBytesUnsigned(std::string_view lhs, std::string_view rhs) {
   const std::size_t common_length = std::min(lhs.size(), rhs.size());
   if (const int prefix_compare = std::memcmp(lhs.data(), rhs.data(), common_length);
       prefix_compare != 0) {
      return prefix_compare <=> 0;
   }
   return lhs.size() <=> rhs.size();
}

// Umbra strings as described in https://www.cidrdb.org/cidr2020/papers/p29-neumann-cidr20.pdf
// aka GermanString as popularized by Andy Pavlo
// But with a templated size
template <size_t I, typename suffix_id_type>
class GermanString {
   // No space left to save prefix if suffix_id_type is too large
   static_assert(I > sizeof(suffix_id_type));

   friend class boost::serialization::access;
   friend class std::hash<rhydb::GermanString<I, suffix_id_type>>;

  public:
   using length_type = uint32_t;
   // Strings up until this size are stored in-place
   constexpr static size_t SHORT_STRING_SIZE = I;
   constexpr static size_t PREFIX_START = sizeof(length_type);
   constexpr static size_t PREFIX_LENGTH = I - sizeof(suffix_id_type);
   constexpr static size_t SUFFIX_ID_START = PREFIX_START + PREFIX_LENGTH;

   template <class Archive>
   void serialize(Archive& archive, [[maybe_unused]] const uint32_t version) {
      // clang-format off
      archive & data;
      // clang-format on
   }

  private:
   std::array<std::byte, I + sizeof(length_type)> data;

   GermanString() = default;

  public:
   GermanString(length_type length, std::string_view prefix, suffix_id_type suffix_offset) {
      RHYDB_ASSERT(length > SHORT_STRING_SIZE);
      RHYDB_ASSERT(prefix.size() == PREFIX_LENGTH);
      RHYDB_ASSERT(PREFIX_START + PREFIX_LENGTH <= data.size());
      RHYDB_ASSERT(SUFFIX_ID_START + sizeof(suffix_id_type) <= data.size());
      *reinterpret_cast<length_type*>(data.data()) = length;
      std::memcpy(data.data() + PREFIX_START, prefix.data(), prefix.size());
      *reinterpret_cast<suffix_id_type*>(data.data() + SUFFIX_ID_START) = suffix_offset;
   }

   explicit GermanString(std::string_view short_string) {
      const length_type short_string_length = short_string.size();
      RHYDB_ASSERT(short_string_length <= SHORT_STRING_SIZE);
      *reinterpret_cast<length_type*>(data.data()) = short_string_length;
      std::memcpy(data.data() + PREFIX_START, short_string.data(), short_string.size());
   }

   [[nodiscard]] length_type length() const {
      return *reinterpret_cast<const length_type*>(data.data());
   }

   [[nodiscard]] bool isInPlace() const { return length() <= I; }

   [[nodiscard]] std::string_view getShortString() const {
      return std::string_view{reinterpret_cast<const char*>(data.data() + PREFIX_START), length()};
   }

   [[nodiscard]] std::string_view prefix() const {
      return std::string_view{
         reinterpret_cast<const char*>(data.data() + PREFIX_START), PREFIX_LENGTH,
      };
   }

   [[nodiscard]] suffix_id_type suffixId() const {
      return *reinterpret_cast<const suffix_id_type*>(data.data() + SUFFIX_ID_START);
   }

   // Compares this to other without looking up the suffix. We might be able to differentiate using
   // only the prefix and length
   [[nodiscard]] std::optional<std::strong_ordering> fastCompare(std::string_view other) const {
      if (length() <= SHORT_STRING_SIZE) {
         return compareBytesUnsigned(this->getShortString(), other);
      }

      const std::string_view other_prefix = other.substr(0, PREFIX_LENGTH);

      const int prefix_compare =
         std::memcmp(this->prefix().data(), other_prefix.data(), other_prefix.size());
      if (prefix_compare < 0) {
         return std::strong_ordering::less;
      }
      if (prefix_compare > 0) {
         return std::strong_ordering::greater;
      }
      // Prefix matches, so we cannot decide without looking at the suffix
      return std::nullopt;
   }
};

using RhyDBString = GermanString<12, storage::vector::VariableDataRegistry::Identifier>;

}  // namespace rhydb
