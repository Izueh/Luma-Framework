#include "Shaders/Database.hpp"

#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Jobs.hpp"
#include "System/Paths.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Shaders {
namespace Database {
namespace {
// Never held with another lock
std::shared_mutex mutex;
std::unordered_map<uint32_t, std::unique_ptr<Shader>> shaders;

struct Names
{
   std::string_view name;
   std::span<const uint32_t> hashes;
};

// Written while Init runs, read without a lock after
std::vector<Names> names;

#if DEVELOPMENT || TEST
const std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::DUMP_SHADERS.key,
      .label = "Dump Shaders",
      .tooltip = "Writes every shader the game creates to Luma/Dump, once (\"0x<hash>.<target>.cso\")",
      .category = "Development",
      .visibility = Settings::Visibility::Development,
      .default_value = true,
   },
};

// Lock order: never held with another lock
std::mutex dump_mutex;
// Created since the last present
std::vector<const Shader*> created;
// Presenting thread
std::unordered_set<uint32_t> dumped;
bool dumped_scanned = false;
bool dumping = false;

// 1.0 doesn't dump a hash again across boots: what's in the folder is done
void ScanDumped()
{
   std::error_code error;
   for (std::filesystem::directory_iterator it(System::Paths::GetDump(), error), end; !error && it != end; it.increment(error))
   {
      const std::wstring name = it->path().filename().wstring();
      uint32_t hash = 0;
      if (it->path().extension() == L".cso" && name.starts_with(L"0x") && swscanf_s(name.c_str() + 2, L"%8x", &hash) == 1)
         dumped.insert(hash);
   }
}
#endif
} // namespace

void Register()
{
#if DEVELOPMENT || TEST
   Settings::Register(SETTINGS, Settings::Kind::Development);
#endif
}

void Name(std::string_view name, std::span<const uint32_t> hashes)
{
   names.push_back({name, hashes});
}

std::string_view GetName(uint32_t hash)
{
   for (const Names& entry : names)
   {
      if (std::ranges::contains(entry.hashes, hash))
         return entry.name;
   }
   return {};
}

Shader* Find(uint32_t hash)
{
   std::shared_lock lock(mutex);
   const auto it = shaders.find(hash);
   return it != shaders.end() ? it->second.get() : nullptr;
}

Shader& Add(std::unique_ptr<Shader> shader)
{
   Shader* kept = nullptr;
   {
      std::scoped_lock lock(mutex);
      auto [it, added] = shaders.try_emplace(shader->hash, std::move(shader));
      kept = it->second.get();
      if (!added)
         return *kept;
   }
#if DEVELOPMENT || TEST
   std::scoped_lock lock(dump_mutex);
   created.push_back(kept);
#endif
   return *kept;
}

std::vector<const Shader*> GetShaders()
{
   std::shared_lock lock(mutex);
   std::vector<const Shader*> list;
   list.reserve(shaders.size());
   for (const auto& [hash, shader] : shaders)
      list.push_back(shader.get());
   return list;
}

std::string GetTarget(std::span<const uint8_t> bytecode)
{
   const auto read = [&](size_t offset) {
      uint32_t value = 0;
      if (offset + sizeof(value) <= bytecode.size())
         std::memcpy(&value, bytecode.data() + offset, sizeof(value));
      return value;
   };
   // "DXBC", a 16 byte checksum, a version, the total size, the chunk count and their offsets
   if (bytecode.size() < 32 || std::memcmp(bytecode.data(), "DXBC", 4) != 0)
      return {};
   const uint32_t chunk_count = read(28);
   for (uint32_t i = 0; i < chunk_count; i++)
   {
      const uint32_t chunk = read(32 + size_t(i) * 4);
      if (size_t(chunk) + 12 > bytecode.size())
         return {};
      // The shader program chunk (SM5, SM4) starts with the version and program type
      if (std::memcmp(bytecode.data() + chunk, "SHEX", 4) != 0 && std::memcmp(bytecode.data() + chunk, "SHDR", 4) != 0)
         continue;
      const uint32_t version = read(size_t(chunk) + 8);
      constexpr std::array<std::string_view, 6> PROGRAMS = {"ps", "vs", "gs", "hs", "ds", "cs"};
      const uint32_t program = version >> 16;
      if (program >= PROGRAMS.size())
         return {};
      return std::format("{}_{}_{}", PROGRAMS[program], (version >> 4) & 0xF, version & 0xF);
   }
   return {};
}

#if DEVELOPMENT || TEST
void QueueDumps()
{
   if (!dumped_scanned)
   {
      dumped_scanned = true;
      ScanDumped();
   }
   // Switched off, what's created meanwhile waits to be dumped once it's on again (1.0)
   if (dumping || !Settings::Get(Settings::Core::DUMP_SHADERS))
      return;
   std::vector<const Shader*> to_dump;
   {
      std::scoped_lock lock(dump_mutex);
      to_dump.swap(created);
   }
   std::erase_if(to_dump, [](const Shader* shader) { return !dumped.insert(shader->hash).second; });
   if (to_dump.empty())
      return;
   dumping = true;
   Jobs::Queue(
      "Dumping shaders",
      [to_dump] {
         for (const Shader* shader : to_dump)
            Dump(*shader);
      },
      [] {
         dumping = false;
         return Jobs::Outcome{};
      });
}

std::filesystem::path Dump(const Shader& shader)
{
   const std::filesystem::path directory = System::Paths::GetDump();
   std::error_code error;
   std::filesystem::create_directories(directory, error);
   const std::filesystem::path path = directory / std::format(L"0x{:08X}{}{}.cso", shader.hash, shader.target.empty() ? L"" : L".", std::wstring(shader.target.begin(), shader.target.end()));
   std::ofstream stream(path, std::ios::binary | std::ios::trunc);
   stream.write(reinterpret_cast<const char*>(shader.bytecode.data()), std::streamsize(shader.bytecode.size()));
   if (!stream)
   {
      Utils::Log::Warning("Shaders: couldn't dump {}", Utils::Strings::ToUtf8(path));
      return {};
   }
   return path;
}
#endif
} // namespace Database
} // namespace Shaders
} // namespace Luma
