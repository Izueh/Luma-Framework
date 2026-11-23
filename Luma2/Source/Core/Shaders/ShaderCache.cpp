#include "Shaders/ShaderCache.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

#include <Windows.h>

#include "System/Paths.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Shaders {
namespace Cache {
namespace {
// Bump when the key or Compiler's flags change, so older entries miss
constexpr uint64_t VERSION = 1;

constexpr uint64_t FNV_OFFSET = 0xCBF29CE484222325ull;
constexpr uint64_t FNV_PRIME = 0x100000001B3ull;

uint64_t Hash(std::string_view text, uint64_t hash)
{
   for (const char c : text)
   {
      hash ^= uint8_t(c);
      hash *= FNV_PRIME;
   }
   return hash;
}

std::filesystem::path GetDirectory()
{
   return System::Paths::GetCache() / L"Shaders";
}

std::optional<std::string> Read(const std::filesystem::path& path)
{
   std::ifstream stream(path, std::ios::binary);
   if (!stream)
      return std::nullopt;
   return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Through a file of the writing thread's, renamed: a crash or another thread never leaves a partial file under the name
void Write(const std::filesystem::path& path, std::string_view data)
{
   std::error_code error;
   std::filesystem::create_directories(path.parent_path(), error);
   std::filesystem::path temporary = path;
   temporary += std::format(L".{}.tmp", GetCurrentThreadId());
   {
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      stream.write(data.data(), std::streamsize(data.size()));
      if (!stream)
      {
         Log::Warning("Shaders: couldn't write {} to the cache", Utils::Strings::ToUtf8(path.filename()));
         return;
      }
   }
   std::filesystem::rename(temporary, path, error);
   if (error)
      std::filesystem::remove(temporary, error);
}

std::filesystem::path GetShaderPath(uint64_t key)
{
   return GetDirectory() / std::format(L"{:016X}.cso", key);
}

std::filesystem::path GetReflectionPath(uint32_t hash)
{
   return GetDirectory() / L"Reflection" / std::format(L"0x{:08X}.json", hash);
}
} // namespace

uint64_t GetKey(std::string_view preprocessed, std::string_view target, std::string_view entry)
{
   uint64_t hash = Hash(std::string_view(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION)), FNV_OFFSET);
   size_t start = 0;
   while (start < preprocessed.size())
   {
      size_t end = preprocessed.find('\n', start);
      end = end == std::string_view::npos ? preprocessed.size() : end + 1;
      const std::string_view line = preprocessed.substr(start, end - start);
      if (!line.starts_with("#line"))
         hash = Hash(line, hash);
      start = end;
   }
   hash = Hash(target, Hash("|", hash));
   return Hash(entry, Hash("|", hash));
}

uint64_t GetPatchKey(uint32_t hash, std::string_view patch)
{
   uint64_t key = Hash("patch|", Hash(std::string_view(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION)), FNV_OFFSET));
   key = Hash(std::string_view(reinterpret_cast<const char*>(&hash), sizeof(hash)), key);
   return Hash(patch, Hash("|", key));
}

std::optional<Bytecode> ReadShader(uint64_t key)
{
   const std::optional<std::string> data = Read(GetShaderPath(key));
   if (!data || data->empty())
      return std::nullopt;
   return Bytecode(data->begin(), data->end());
}

void WriteShader(uint64_t key, std::span<const uint8_t> bytecode)
{
   Write(GetShaderPath(key), std::string_view(reinterpret_cast<const char*>(bytecode.data()), bytecode.size()));
}

std::optional<std::string> ReadReflection(uint32_t hash)
{
   return Read(GetReflectionPath(hash));
}

void WriteReflection(uint32_t hash, std::string_view text)
{
   Write(GetReflectionPath(hash), text);
}

std::optional<std::string> ReadPatchIndex()
{
   return Read(GetDirectory() / L"Patches.json");
}

void WritePatchIndex(std::string_view text)
{
   Write(GetDirectory() / L"Patches.json", text);
}
} // namespace Cache
} // namespace Shaders
} // namespace Luma
