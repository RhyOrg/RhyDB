#pragma once

#include <array>
#include <cstdint>

#include <boost/serialization/access.hpp>

namespace rhydb {

template <typename SymbolType, typename T>
class SymbolMap {
   friend class boost::serialization::access;

  private:
   template <class Archive>
   void serialize(Archive& archive, const uint32_t /*version*/) {
      // clang-format off
      archive & data;
      // clang-format on
   }

   std::array<T, SymbolType::COUNT> data;

  public:
   SymbolMap() = default;

   explicit SymbolMap(std::array<T, SymbolType::COUNT>&& data)
       : data(data) {}

   T& operator[](SymbolType::Symbol symbol) { return data.at(static_cast<uint8_t>(symbol)); }

   [[nodiscard]] const T& at(SymbolType::Symbol symbol) const {
      return data.at(static_cast<uint8_t>(symbol));
   }
};

}  // namespace rhydb
