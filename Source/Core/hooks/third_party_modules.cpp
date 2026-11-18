#include "hooks/third_party_modules.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>

#include <Windows.h>
#include <Psapi.h>

#include "utils/modules.hpp"
#include "utils/log.hpp"
#include "utils/strings.hpp"

namespace Luma {
namespace Hooks {
namespace ThirdPartyModules {
namespace {
enum class Kind : uint8_t
{
   Overlay,
   Tool,
};

struct Known
{
   std::wstring_view name; // The file name without ".dll", or its start when it ends with '*'
   std::string_view label;
   Kind kind;
};

constexpr std::array KNOWN = {
   Known{L"GameOverlayRenderer64", "Steam overlay", Kind::Overlay},
   Known{L"GameOverlayRenderer", "Steam overlay", Kind::Overlay},
   Known{L"RTSSHooks64", "RivaTuner Statistics Server", Kind::Overlay},
   Known{L"RTSSHooks", "RivaTuner Statistics Server", Kind::Overlay},
   Known{L"DiscordHook64", "Discord overlay", Kind::Overlay},
   Known{L"DiscordHook", "Discord overlay", Kind::Overlay},
   Known{L"nvspcap64", "NVIDIA overlay capture", Kind::Overlay},
   Known{L"nvspcap", "NVIDIA overlay capture", Kind::Overlay},
   Known{L"NvCamera64", "NVIDIA Ansel/Freestyle", Kind::Overlay},
   Known{L"graphics-hook64", "OBS game capture", Kind::Overlay},
   Known{L"graphics-hook32", "OBS game capture", Kind::Overlay},
   Known{L"EOSOVH-Win64-Shipping", "Epic overlay", Kind::Overlay},
   Known{L"ReShade64", "ReShade", Kind::Overlay},
   Known{L"ReShade32", "ReShade", Kind::Overlay},
   Known{L"SpecialK64", "Special K", Kind::Overlay},
   Known{L"SpecialK32", "Special K", Kind::Overlay},
   Known{L"renderdoc", "RenderDoc", Kind::Tool},
   Known{L"sl.*", "Streamline", Kind::Tool},
};

struct Module
{
   std::string name;
   uintptr_t begin;
   uintptr_t end;
};

// Lock order: never held with another lock, nothing is called with it held
std::shared_mutex mutex;
std::vector<Module> modules;
// The fast "none" of Find, read without the lock
std::atomic<uint32_t> count = 0;

const Known* FindKnown(std::wstring_view file_name)
{
   std::wstring_view name = file_name;
   if (name.size() > 4 && Utils::Strings::EqualsIgnoreCase(name.substr(name.size() - 4), L".dll"))
      name.remove_suffix(4);
   for (const Known& known : KNOWN)
   {
      if (known.name.ends_with(L'*'))
      {
         const std::wstring_view prefix = known.name.substr(0, known.name.size() - 1);
         if (name.size() > prefix.size() && Utils::Strings::EqualsIgnoreCase(name.substr(0, prefix.size()), prefix))
            return &known;
      }
      else if (Utils::Strings::EqualsIgnoreCase(name, known.name))
      {
         return &known;
      }
   }
   return nullptr;
}

void Remember(std::string name, const void* base, size_t size)
{
   std::unique_lock lock(mutex);
   const auto begin = reinterpret_cast<uintptr_t>(base);
   if (std::ranges::any_of(modules, [begin](const Module& module) { return module.begin == begin; }))
      return;
   modules.push_back({std::move(name), begin, begin + size});
   count = uint32_t(modules.size());
}

void Classify(std::wstring_view file_name, const void* base, size_t size)
{
   const Known* known = FindKnown(file_name);
   std::string_view label = known ? known->label : std::string_view();
   Kind kind = known ? known->kind : Kind::Overlay;
   // ReShade under any name (as dxgi.dll, d3d11.dll...) exports its version
   if (!known && Utils::Modules::FindExport(static_cast<HMODULE>(const_cast<void*>(base)), "ReShadeVersion"))
      label = "ReShade";
   if (label.empty())
      return;
   const std::string name = Utils::Strings::ToUtf8(file_name);
   Utils::Log::Info("Overlays: {} ({}) at {}{}", label, name, base, kind == Kind::Tool ? ", a tool: its calls count as the game's" : "");
   if (kind == Kind::Overlay)
      Remember(name, base, size);
}
} // namespace

void OnModuleLoaded(std::wstring_view name, const void* base, size_t size)
{
   Classify(name, base, size);
}

void OnModuleUnloaded(const void* base)
{
   std::unique_lock lock(mutex);
   const auto begin = reinterpret_cast<uintptr_t>(base);
   std::erase_if(modules, [begin](const Module& module) { return module.begin == begin; });
   count = uint32_t(modules.size());
}

void ScanLoadedModules()
{
   std::vector<HMODULE> loaded(256);
   DWORD needed = 0;
   while (EnumProcessModules(GetCurrentProcess(), loaded.data(), DWORD(loaded.size() * sizeof(HMODULE)), &needed) && needed > loaded.size() * sizeof(HMODULE))
      loaded.resize(needed / sizeof(HMODULE));
   loaded.resize(needed / sizeof(HMODULE));
   for (HMODULE module : loaded)
   {
      MODULEINFO info = {};
      wchar_t name[MAX_PATH];
      const DWORD length = GetModuleBaseNameW(GetCurrentProcess(), module, name, MAX_PATH);
      if (length && GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
         Classify(std::wstring_view(name, length), info.lpBaseOfDll, info.SizeOfImage);
   }
}

void Add(std::string name, const void* base, size_t size)
{
   Remember(std::move(name), base, size);
}

std::string Find(const void* address)
{
   if (count.load(std::memory_order_relaxed) == 0)
      return {};
   const auto at = reinterpret_cast<uintptr_t>(address);
   std::shared_lock lock(mutex);
   for (const Module& module : modules)
   {
      if (at >= module.begin && at < module.end)
         return module.name;
   }
   return {};
}

uintptr_t FindBase(const void* address)
{
   if (count.load(std::memory_order_relaxed) == 0)
      return 0;
   const auto at = reinterpret_cast<uintptr_t>(address);
   std::shared_lock lock(mutex);
   for (const Module& module : modules)
   {
      if (at >= module.begin && at < module.end)
         return module.begin;
   }
   return 0;
}

std::vector<std::string> GetLoaded()
{
   std::shared_lock lock(mutex);
   std::vector<std::string> names;
   for (const Module& module : modules)
      names.push_back(module.name);
   return names;
}
} // namespace ThirdPartyModules
} // namespace Hooks
} // namespace Luma
