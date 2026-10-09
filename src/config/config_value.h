#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "rhydb/common/panic.h"

namespace rhydb::config {

enum class ConfigValueType : uint8_t { STRING, PATH, INT32, UINT32, UINT16, BOOL, LIST };

constexpr std::string_view configValueTypeToString(ConfigValueType type) {
   switch (type) {
      case ConfigValueType::STRING:
         return "string";
      case ConfigValueType::PATH:
         return "path";
      case ConfigValueType::INT32:
         return "i32";
      case ConfigValueType::UINT32:
         return "u32";
      case ConfigValueType::UINT16:
         return "u16";
      case ConfigValueType::BOOL:
         return "bool";
      case ConfigValueType::LIST:
         return "list";
   }
   RHYDB_UNREACHABLE();
}

class ConfigValue {
   explicit ConfigValue(
      std::variant<
         std::string,
         std::filesystem::path,
         int32_t,
         uint32_t,
         uint16_t,
         bool,
         std::vector<std::string>> value
   )
       : value(std::move(value)) {}

  public:
   std::variant<
      std::string,
      std::filesystem::path,
      int32_t,
      uint32_t,
      uint16_t,
      bool,
      std::vector<std::string>>
      value;

   static ConfigValue fromString(const std::string& value) { return ConfigValue{value}; }

   static ConfigValue fromPath(const std::filesystem::path& value) {
      ConfigValue result{value};
      RHYDB_ASSERT(get_if<std::filesystem::path>(&result.value) != nullptr);
      return result;
   }

   static ConfigValue fromInt32(int32_t value) { return ConfigValue{value}; }

   static ConfigValue fromUint32(uint32_t value) { return ConfigValue{value}; }

   static ConfigValue fromUint16(uint16_t value) { return ConfigValue{value}; }

   static ConfigValue fromBool(bool value) { return ConfigValue{value}; }

   static ConfigValue fromList(const std::vector<std::string>& value) { return ConfigValue{value}; }

   [[nodiscard]] ConfigValueType getValueType() const;

   [[nodiscard]] std::string toString() const;
};

}  // namespace rhydb::config
