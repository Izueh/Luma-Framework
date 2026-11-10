#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// Luma's settings file (Luma.toml next to the Luma dll), with the shapes of ReShade's "get_config_value()" and "set_config_value()"
namespace Config
{
   using Value = std::variant<bool, int64_t, double, std::string, std::vector<std::string>>;

   void Load(const std::filesystem::path& path);
   // Writes the file if any value changed since the last save
   void Save();

   std::optional<Value> GetValue(std::string_view section, std::string_view key);
   void SetValue(std::string_view section, std::string_view key, Value value);
   void Remove(std::string_view section, std::string_view key);

   // Without "value", "size" is set to the needed size. Otherwise "size" is the buffer size, and is set to the copied length.
   // Array elements are each followed by a null character.
   bool Get(const char* section, const char* key, char* value, size_t* size);

   template <typename T>
      requires std::is_arithmetic_v<T>
   bool Get(const char* section, const char* key, T& value)
   {
      const std::optional<Value> stored = GetValue(section, key);
      if (!stored)
         return false;
      if (const bool* stored_bool = std::get_if<bool>(&*stored))
      {
         value = static_cast<T>(*stored_bool ? 1 : 0);
         return true;
      }
      if (const int64_t* stored_int = std::get_if<int64_t>(&*stored))
      {
         if constexpr (std::is_same_v<T, bool>)
            value = *stored_int != 0;
         else
            value = static_cast<T>(*stored_int);
         return true;
      }
      if (const double* stored_float = std::get_if<double>(&*stored))
      {
         if constexpr (std::is_same_v<T, bool>)
            value = *stored_float != 0.0;
         else
            value = static_cast<T>(*stored_float);
         return true;
      }
      if (const std::string* stored_string = std::get_if<std::string>(&*stored))
      {
         if constexpr (std::is_same_v<T, bool>)
         {
            int64_t parsed = 0;
            if (std::from_chars(stored_string->data(), stored_string->data() + stored_string->size(), parsed).ec != std::errc{})
               return false;
            value = parsed != 0;
            return true;
         }
         else
         {
            return std::from_chars(stored_string->data(), stored_string->data() + stored_string->size(), value).ec == std::errc{};
         }
      }
      return false;
   }

   // Null removes the key
   void Set(const char* section, const char* key, const char* value);
   // Splits "value" on null characters into an array
   void Set(const char* section, const char* key, const char* value, size_t size);

   template <typename T>
      requires std::is_arithmetic_v<T>
   void Set(const char* section, const char* key, const T& value)
   {
      if constexpr (std::is_same_v<T, bool>)
         SetValue(section, key, value);
      else if constexpr (std::is_floating_point_v<T>)
         SetValue(section, key, static_cast<double>(value));
      else if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(int64_t))
      {
         if (value > static_cast<T>(INT64_MAX))
            SetValue(section, key, std::to_string(value));
         else
            SetValue(section, key, static_cast<int64_t>(value));
      }
      else
         SetValue(section, key, static_cast<int64_t>(value));
   }
}
