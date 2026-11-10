#include "utils/toml.hpp"

#include <fstream>
#include <sstream>

#include <glaze/toml.hpp>

#include "utils/log.hpp"
#include "utils/strings.hpp"

namespace Luma {
namespace Utils {
namespace Toml {
namespace {
bool IsBareKey(std::string_view key)
{
   if (key.empty())
      return false;
   for (const char c : key)
   {
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
         return false;
   }
   return true;
}

std::string FormatKey(std::string_view key)
{
   if (IsBareKey(key))
      return std::string(key);
   return glz::write_toml(std::string(key)).value_or("\"\"");
}

// Sections are written here: glaze writes maps as inline tables, which are hard to edit
void WriteTable(std::string& text, const std::string& path, const Table& table)
{
   for (const auto& [key, value] : table)
   {
      if (!value.is_object())
         text += std::format("{} = {}\n", FormatKey(key), glz::write_toml(value).value_or(""));
   }
   for (const auto& [key, value] : table)
   {
      if (!value.is_object())
         continue;
      const std::string child_path = path.empty() ? FormatKey(key) : std::format("{}.{}", path, FormatKey(key));
      if (!text.empty())
         text += '\n';
      text += std::format("[{}]\n", child_path);
      WriteTable(text, child_path, value.get_object());
   }
}
} // namespace

bool Read(std::string_view text, Table& document, std::string* error)
{
   // As a map: glaze 9.1's schema-less root rejects documents that start with a "[table]" header
   Table result;
   const glz::error_ctx read_error = glz::read_toml(result, text);
   if (read_error)
   {
      if (error)
         *error = glz::format_error(read_error, text);
      return false;
   }
   document = std::move(result);
   return true;
}

std::string Write(const Table& document)
{
   std::string text;
   WriteTable(text, {}, document);
   return text;
}

bool Load(const std::filesystem::path& path, Table& document)
{
   std::error_code exists_error;
   if (!std::filesystem::exists(path, exists_error))
   {
      document = {};
      return true;
   }
   std::ifstream file(path, std::ios::binary);
   if (!file)
   {
      Log::Error("Toml: failed to open {}", Strings::ToUtf8(path));
      return false;
   }
   std::stringstream text;
   text << file.rdbuf();
   std::string error;
   if (!Read(text.str(), document, &error))
   {
      Log::Error("Toml: failed to parse {}:\n{}", Strings::ToUtf8(path), error);
      return false;
   }
   return true;
}

bool Save(const std::filesystem::path& path, const Table& document)
{
   std::ofstream file(path, std::ios::binary | std::ios::trunc);
   if (!file)
   {
      Log::Error("Toml: failed to write {}", Strings::ToUtf8(path));
      return false;
   }
   file << Write(document);
   return bool(file);
}
} // namespace Toml
} // namespace Utils
} // namespace Luma
