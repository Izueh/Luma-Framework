#include "Shaders/Files.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cwctype>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_set>

#include "Utils/Debug.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Shaders {
namespace Files {
namespace {
constexpr std::string_view EXTENSION = ".hlsl";
constexpr std::string_view LUMA_PREFIX = "Luma_";
constexpr std::string_view REDIRECTED_HASH = "########";
constexpr size_t HASH_LENGTH = 8;
// "ps_5_0", at the end of a replacement's name
constexpr size_t TARGET_LENGTH = 6;

// Written while Init runs, read without a lock after
std::vector<Redirect> redirects;

// As 1.0's std::stoul: the hex digits the text starts with
std::optional<uint32_t> ParseHash(std::string_view text)
{
   uint32_t value = 0;
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
   if (error != std::errc())
      return std::nullopt;
   return value;
}

bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix)
{
   return text.size() >= prefix.size() &&
      std::ranges::equal(text.substr(0, prefix.size()), prefix, [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); });
}

} // namespace

void AddRedirect(std::string_view name, std::span<const uint32_t> hashes)
{
   redirects.push_back({name, hashes});
}

std::optional<File> Parse(std::string_view path, std::span<const Redirect> declared)
{
   if (StartsWithIgnoreCase(path, "Includes/") || StartsWithIgnoreCase(path, "Game/") || !path.ends_with(EXTENSION))
      return std::nullopt;
   const size_t slash = path.rfind('/');
   std::string_view name = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
   name.remove_suffix(EXTENSION.size());
   const bool has_hash = name.find("0x") != std::string_view::npos;
   const bool is_luma = name.starts_with(LUMA_PREFIX);
   ASSERT_ONCE_MSG(!is_luma || !has_hash, "Luma's own shaders start with \"Luma_\" and have no hash, a replacement doesn't start with it");

   File file = {.path = std::string(path), .name = std::string(name)};
   // Luma's passes, by their name: anything under Luma/ (1.0's Global folder), or a game's "Luma_" file
   if (StartsWithIgnoreCase(path, "Luma/") || (is_luma && !has_hash))
   {
      file.kind = Kind::Custom;
      return file;
   }
   // A name before the hash, the hash, a dot and the target ("_0x12345678.ps_5_0" at least)
   if (!has_hash || name.size() < 2 + HASH_LENGTH + 1 + TARGET_LENGTH)
      return std::nullopt;
   ASSERT_ONCE_MSG(name.size() > 2 + HASH_LENGTH + 1 + TARGET_LENGTH, "A replacement has a name before its hash, to tell it from a dumped shader");
   file.target = std::string(name.substr(name.size() - TARGET_LENGTH));
   if (file.target[2] != '_' || file.target[4] != '_')
      return std::nullopt;
   const size_t first_hash = name.find("0x");
   for (size_t position = first_hash; position != std::string_view::npos; position = name.find("0x", position + 1))
   {
      const std::string_view text = name.substr(position + 2, HASH_LENGTH);
      if (text != REDIRECTED_HASH)
      {
         if (const std::optional<uint32_t> value = ParseHash(text))
            file.hashes.push_back({*value, std::string(text)});
         continue;
      }
      // The hashes the game listed for the name before the first hash
      std::string_view redirected_name = name.substr(0, first_hash);
      if (redirected_name.ends_with('_'))
         redirected_name.remove_suffix(1);
      for (const Redirect& redirect : declared)
      {
         if (redirect.name != redirected_name)
            continue;
         for (const uint32_t hash : redirect.hashes)
            file.hashes.push_back({hash, std::format("{:08X}", hash)});
      }
   }
   return file;
}

std::vector<Declaration> ParseDeclarations(std::string_view text, std::string_view name)
{
   constexpr std::string_view PREFIX = "// Luma:";
   std::vector<Declaration> declarations;
   size_t start = 0;
   while (start < text.size())
   {
      size_t end = text.find('\n', start);
      if (end == std::string_view::npos)
         end = text.size();
      std::string_view line = text.substr(start, end - start);
      start = end + 1;
      line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
      if (!line.starts_with(PREFIX))
         continue;
      line.remove_prefix(PREFIX.size());
      Declaration declaration;
      bool named = false;
      size_t token_start = 0;
      while ((token_start = line.find_first_not_of(" \t\r", token_start)) != std::string_view::npos)
      {
         const size_t token_end = std::min(line.find_first_of(" \t\r", token_start), line.size());
         const std::string_view token = line.substr(token_start, token_end - token_start);
         token_start = token_end;
         const size_t equals = token.find('=');
         if (declaration.target.empty())
            declaration.target = token;
         else if (equals == std::string_view::npos)
         {
            declaration.name = token;
            named = true;
         }
         else if (token.substr(0, equals) == "entry")
            declaration.entry = token.substr(equals + 1);
         else
            declaration.defines.emplace_back(token.substr(0, equals), token.substr(equals + 1));
      }
      const bool valid = declaration.target.size() == TARGET_LENGTH && declaration.target[2] == '_' && declaration.target[4] == '_';
      ASSERT_ONCE_MSG(valid, "A Luma declaration line starts with its target, like \"// Luma: ps_5_0\"");
      if (!valid)
         continue;
      if (!named)
         declaration.name = name;
      declarations.push_back(std::move(declaration));
   }
   return declarations;
}

std::vector<File> Scan(std::span<const std::filesystem::path> roots, std::vector<std::string>* overridden)
{
   std::vector<File> files;
   std::unordered_set<std::wstring> seen;
   for (const std::filesystem::path& root : roots)
   {
      std::error_code error;
      for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
      {
         if (!it->is_regular_file(error))
            continue;
         std::wstring relative = it->path().lexically_relative(root).generic_wstring();
         std::wstring key = relative;
         std::ranges::transform(key, key.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
         if (!seen.insert(std::move(key)).second)
         {
            if (overridden)
               overridden->push_back(Utils::Strings::ToUtf8(std::wstring_view(relative)));
            continue;
         }
         std::optional<File> file = Parse(Utils::Strings::ToUtf8(std::wstring_view(relative)), redirects);
         if (!file)
            continue;
         if (file->kind == Kind::Custom)
         {
            std::ifstream stream(it->path(), std::ios::binary);
            file->declarations = ParseDeclarations(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()), file->name);
         }
         files.push_back(std::move(*file));
      }
   }
   std::ranges::sort(files, {}, &File::path);
   return files;
}
} // namespace Files
} // namespace Shaders
} // namespace Luma
