#include "Hooks/Vtables.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "Log/Log.hpp"
#include "System/Modules.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Hooks {
namespace Vtables {
namespace {
using SlotMethod = std::pair<uint32_t, void*>;

struct Entry
{
   // A copy, the vtable itself can be freed with its object
   std::vector<void*> methods;
   std::vector<IID> supported_versions;
   // Methods already classified, so a vtable is classified once: patches tolerated, and real variants
   std::vector<SlotMethod> third_party;
   std::vector<SlotMethod> variants;
};

// Lock order: never held while taking the loader lock (GetName, ClassifyMethod): the loader notification holds it installing hooks here
std::mutex mutex;
std::array<Entry, size_t(Interface::Count)> entries;

struct ApiModule
{
   std::wstring_view name;
   // A proxy takes these names in the game's folder (ReShade, DXVK, dgVoodoo, Luma): only the system's copy is the API
   bool system_only;
};

// The dlls D3D's and DXGI's objects have their methods in: one runtime, its debug layer, another implementation
constexpr std::array API_MODULES = {
   ApiModule{L"dxgi.dll", true},
   ApiModule{L"dxgidebug.dll", true},
   ApiModule{L"d3d11.dll", true},
   ApiModule{L"d3d11_3SDKLayers.dll", true},
   ApiModule{L"d3d11on12.dll", true},
   ApiModule{L"d3d10warp.dll", true},
   ApiModule{L"d3d12.dll", true},
   // The Agility SDK ships these with the game
   ApiModule{L"D3D12Core.dll", false},
   ApiModule{L"d3d12SDKLayers.dll", false},
};

void* const* GetVtable(IUnknown* object)
{
   return *reinterpret_cast<void* const* const*>(object);
}

HMODULE GetModule(const void* address)
{
   HMODULE module = nullptr;
   GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module);
   return module;
}

bool IsApiModule(HMODULE module)
{
   if (!module)
      return false;
   const std::wstring name = System::Modules::GetPath(module).filename().native();
   const auto it = std::ranges::find_if(API_MODULES, [&name](const ApiModule& api) { return Utils::Strings::EqualsIgnoreCase(name, api.name); });
   return it != API_MODULES.end() && (!it->system_only || System::Modules::IsSystemModule(module));
}

enum class Comparison : uint8_t
{
   Same,
   Different,
   Unknown,
};

// With the lock held. A class's objects can have different vtables (D3D11 contexts have one each) but share the methods
Comparison Compare(const Entry& entry, void* const* vtable, std::vector<SlotMethod>& unknown)
{
   unknown.clear();
   for (uint32_t i = 0; i < entry.methods.size(); i++)
   {
      if (entry.methods[i] == vtable[i])
         continue;
      const SlotMethod method = {i, vtable[i]};
      if (std::ranges::find(entry.third_party, method) != entry.third_party.end())
         continue;
      if (std::ranges::find(entry.variants, method) != entry.variants.end())
         return Comparison::Different;
      unknown.push_back(method);
   }
   return unknown.empty() ? Comparison::Same : Comparison::Unknown;
}

// Without the lock: classifies the methods Compare didn't know and records them
bool Classify(Interface interface_type, std::string_view name, const std::vector<SlotMethod>& unknown)
{
   std::vector<void*> recorded;
   {
      std::scoped_lock lock(mutex);
      recorded = entries[size_t(interface_type)].methods;
   }
   std::vector<Method> kinds;
   for (const SlotMethod& method : unknown)
      kinds.push_back(ClassifyMethod(recorded[method.first], method.second));
   // A wrapper object (ReShade's, Streamline's) has its own IUnknown: its methods are another instance, as before, not patched slots
   uint32_t third_party_unknown = 0;
   for (size_t i = 0; i < unknown.size(); i++)
      third_party_unknown += unknown[i].first < 3 && kinds[i] == Method::ThirdParty;
   const bool is_wrapper = third_party_unknown == 3;
   std::vector<SlotMethod> third_party;
   std::vector<SlotMethod> variants;
   for (size_t i = 0; i < unknown.size(); i++)
   {
      if (!is_wrapper && kinds[i] == Method::ThirdParty)
         third_party.push_back(unknown[i]);
      else
         variants.push_back(unknown[i]);
   }
   {
      std::scoped_lock lock(mutex);
      Entry& entry = entries[size_t(interface_type)];
      entry.third_party.insert(entry.third_party.end(), third_party.begin(), third_party.end());
      entry.variants.insert(entry.variants.end(), variants.begin(), variants.end());
   }
   if (!third_party.empty())
   {
      Log::Info("Vtables: {} has {} methods patched by {} (slot {} first), the hooks on the recorded ones still see their calls", name, third_party.size(),
         System::Modules::GetName(third_party.front().second), third_party.front().first);
   }
   return variants.empty();
}

// The first object's methods may already be patched by a tool loaded before Luma: hooked as they are, but said once
void LogPatchedMethods(std::string_view name, const std::vector<void*>& methods)
{
   const HMODULE api = GetModule(methods.size() > 3 ? methods[3] : nullptr);
   for (uint32_t i = 0; i < methods.size(); i++)
   {
      const HMODULE module = GetModule(methods[i]);
      if (module != api && !IsApiModule(module))
      {
         Log::Warning("Vtables: {} slot {} is in {} when first seen, patched before Luma: its hook goes on that tool's function", name, i,
            System::Modules::GetName(methods[i]));
         return;
      }
   }
}
} // namespace

Method ClassifyMethod(const void* recorded, const void* observed)
{
   if (recorded == observed)
      return Method::Same;
   const HMODULE module = GetModule(observed);
   return module && (module == GetModule(recorded) || IsApiModule(module)) ? Method::Variant : Method::ThirdParty;
}

bool Set(Interface interface_type, IUnknown* object, std::span<const InterfaceVersion> versions)
{
   const std::string_view name = versions.front().name;
   void* const* vtable = GetVtable(object);
   std::vector<SlotMethod> unknown;
   {
      std::scoped_lock lock(mutex);
      const Entry& entry = entries[size_t(interface_type)];
      if (!entry.methods.empty())
      {
         const Comparison comparison = Compare(entry, vtable, unknown);
         if (comparison != Comparison::Unknown)
            return comparison == Comparison::Same;
      }
   }
   if (!unknown.empty())
   {
      if (Classify(interface_type, name, unknown))
         return true;
      Log::Warning("Vtables: {} at {} in {} has different methods from the first one, its functions may not be hooked",
         name, static_cast<const void*>(vtable), System::Modules::GetName(vtable));
      return false;
   }

   // Without the lock: this calls into the API
   std::vector<IID> supported_versions;
   std::string_view highest_version = "unknown";
   uint32_t method_count = 0;
   for (const InterfaceVersion& version : versions)
   {
      IUnknown* versioned = nullptr;
      if (SUCCEEDED(object->QueryInterface(version.iid, reinterpret_cast<void**>(&versioned))) && versioned)
      {
         versioned->Release();
         supported_versions.push_back(version.iid);
         highest_version = version.name;
         method_count = (std::max)(method_count, version.method_count);
      }
   }
   std::vector<void*> methods(vtable, vtable + method_count);
   {
      std::scoped_lock lock(mutex);
      Entry& entry = entries[size_t(interface_type)];
      // Recorded by another thread meanwhile
      if (!entry.methods.empty())
      {
         const Comparison comparison = Compare(entry, vtable, unknown);
         if (comparison != Comparison::Unknown)
            return comparison == Comparison::Same;
      }
      else
      {
         entry = {.methods = methods, .supported_versions = std::move(supported_versions)};
      }
   }
   if (!unknown.empty())
      return Classify(interface_type, name, unknown);
   // The vtable may be in heap memory (D3D11 contexts), its methods never are
   Log::Info("Vtables: {} ({}) at {} in {}, methods in {}", name, highest_version, static_cast<const void*>(vtable),
      System::Modules::GetName(vtable), System::Modules::GetName(vtable[3]));
   LogPatchedMethods(name, methods);
   return true;
}

bool Matches(Interface interface_type, IUnknown* object)
{
   void* const* vtable = GetVtable(object);
   std::vector<SlotMethod> unknown;
   {
      std::scoped_lock lock(mutex);
      const Entry& entry = entries[size_t(interface_type)];
      if (entry.methods.empty())
         return false;
      const Comparison comparison = Compare(entry, vtable, unknown);
      if (comparison != Comparison::Unknown)
         return comparison == Comparison::Same;
   }
   return Classify(interface_type, "a vtable", unknown);
}

void* const* Get(Interface interface_type)
{
   std::scoped_lock lock(mutex);
   const Entry& entry = entries[size_t(interface_type)];
   // The vector is never changed once set, so its data stays valid
   return entry.methods.empty() ? nullptr : entry.methods.data();
}

void* GetMethod(Interface interface_type, uint32_t slot)
{
   std::scoped_lock lock(mutex);
   const Entry& entry = entries[size_t(interface_type)];
   return slot < entry.methods.size() ? entry.methods[slot] : nullptr;
}

bool Supports(Interface interface_type, const IID& version)
{
   std::scoped_lock lock(mutex);
   for (const IID& supported : entries[size_t(interface_type)].supported_versions)
   {
      if (supported == version)
         return true;
   }
   return false;
}
} // namespace Vtables
} // namespace Hooks
} // namespace Luma
