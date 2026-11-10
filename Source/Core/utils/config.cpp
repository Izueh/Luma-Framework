#include "utils/config.hpp"

#include <cstring>
#include <mutex>

#include "utils/toml.hpp"

namespace Config
{
   namespace
   {
      std::mutex mutex;
      std::filesystem::path file_path;
      Luma::Utils::Toml::Table document;
      bool dirty = false;

      Luma::Utils::Toml::Value ToToml(const Value& value)
      {
         Luma::Utils::Toml::Value result;
         if (const bool* boolean = std::get_if<bool>(&value))
            result = *boolean;
         else if (const int64_t* integer = std::get_if<int64_t>(&value))
            result = *integer;
         else if (const double* number = std::get_if<double>(&value))
            result = *number;
         else if (const std::string* string = std::get_if<std::string>(&value))
            result = *string;
         else
         {
            Luma::Utils::Toml::Value::array_t array;
            for (const std::string& element : std::get<std::vector<std::string>>(value))
               array.emplace_back(element);
            result = std::move(array);
         }
         return result;
      }

      std::optional<Value> FromToml(const Luma::Utils::Toml::Value& value)
      {
         if (value.is_boolean())
            return value.get<bool>();
         if (value.holds<int64_t>())
            return value.get<int64_t>();
         if (value.holds<double>())
            return value.get<double>();
         if (value.is_string())
            return value.get<std::string>();
         if (value.is_array())
         {
            std::vector<std::string> elements;
            for (const Luma::Utils::Toml::Value& element : value.get_array())
            {
               if (!element.is_string())
                  return std::nullopt;
               elements.push_back(element.get<std::string>());
            }
            return elements;
         }
         return std::nullopt;
      }

      // Called with the lock held
      const Luma::Utils::Toml::Value* Find(std::string_view section, std::string_view key)
      {
         const auto section_it = document.find(section);
         if (section_it == document.end() || !section_it->second.is_object())
            return nullptr;
         const auto& table = section_it->second.get_object();
         const auto key_it = table.find(key);
         return key_it != table.end() ? &key_it->second : nullptr;
      }
   }

   void Load(const std::filesystem::path& path)
   {
      std::scoped_lock lock(mutex);
      file_path = path;
      Luma::Utils::Toml::Load(path, document);
      dirty = false;
   }

   void Save()
   {
      std::scoped_lock lock(mutex);
      if (!dirty || file_path.empty())
         return;
      if (Luma::Utils::Toml::Save(file_path, document))
         dirty = false;
   }

   std::optional<Value> GetValue(std::string_view section, std::string_view key)
   {
      std::scoped_lock lock(mutex);
      const Luma::Utils::Toml::Value* value = Find(section, key);
      return value ? FromToml(*value) : std::nullopt;
   }

   void SetValue(std::string_view section, std::string_view key, Value value)
   {
      std::scoped_lock lock(mutex);
      Luma::Utils::Toml::Value& section_value = document[std::string(section)];
      if (!section_value.is_object())
         section_value = Luma::Utils::Toml::Table{};
      section_value.get_object()[std::string(key)] = ToToml(value);
      dirty = true;
   }

   void Remove(std::string_view section, std::string_view key)
   {
      std::scoped_lock lock(mutex);
      const auto section_it = document.find(section);
      if (section_it == document.end() || !section_it->second.is_object())
         return;
      auto& table = section_it->second.get_object();
      const auto key_it = table.find(key);
      if (key_it == table.end())
         return;
      table.erase(key_it);
      dirty = true;
   }

   bool Get(const char* section, const char* key, char* value, size_t* size)
   {
      const std::optional<Value> stored = GetValue(section ? section : "", key ? key : "");
      std::vector<std::string> elements;
      if (stored)
      {
         if (const std::vector<std::string>* array = std::get_if<std::vector<std::string>>(&*stored))
            elements = *array;
         else if (const std::string* string = std::get_if<std::string>(&*stored))
            elements.push_back(*string);
         else if (const bool* boolean = std::get_if<bool>(&*stored))
            elements.push_back(*boolean ? "1" : "0");
         else if (const int64_t* integer = std::get_if<int64_t>(&*stored))
            elements.push_back(std::to_string(*integer));
         else if (const double* number = std::get_if<double>(&*stored))
         {
            char buffer[32] = "";
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, *number);
            elements.emplace_back(buffer, result.ptr);
         }
      }

      if (size != nullptr)
      {
         std::string joined;
         for (const std::string& element : elements)
         {
            joined += element;
            joined += '\0';
         }
         if (elements.empty())
            *size = 0;
         else if (value == nullptr)
            *size = joined.size() + 1;
         else if (*size != 0)
         {
            *size = joined.copy(value, *size - 1);
            value[*size] = '\0';
         }
      }
      return !elements.empty();
   }

   void Set(const char* section, const char* key, const char* value)
   {
      Set(section, key, value, value != nullptr ? std::strlen(value) : 0);
   }

   void Set(const char* section, const char* key, const char* value, size_t size)
   {
      if (value == nullptr)
      {
         Remove(section ? section : "", key ? key : "");
         return;
      }
      std::vector<std::string> elements;
      for (size_t i = 0, k = 0; i < size; i++)
      {
         if (k >= elements.size())
            elements.resize(k + 1);
         if (value[i] == '\0')
         {
            k++;
            continue;
         }
         elements[k] += value[i];
      }
      while (elements.size() > 1 && elements.back().empty())
         elements.pop_back();
      if (elements.size() == 1)
         SetValue(section ? section : "", key ? key : "", std::move(elements[0]));
      else
         SetValue(section ? section : "", key ? key : "", std::move(elements));
   }
}
