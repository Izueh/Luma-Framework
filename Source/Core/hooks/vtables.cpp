#include "hooks/vtables.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

#include "utils/log.hpp"
#include "utils/modules.hpp"

namespace Luma {
namespace Hooks {
namespace Vtables {
namespace {
struct Entry
{
   // A copy, the vtable itself can be freed with its object
   std::vector<void*> methods;
   std::vector<IID> supported_versions;
};

std::mutex mutex;
std::array<Entry, size_t(Interface::Count)> entries;

void* const* GetVtable(IUnknown* object)
{
   return *reinterpret_cast<void* const* const*>(object);
}

// Objects of the same class can have different vtables (D3D11 contexts have one each, in heap memory), but share the methods
bool SameMethods(const std::vector<void*>& recorded, void* const* vtable)
{
   for (size_t i = 0; i < recorded.size(); i++)
   {
      if (recorded[i] != vtable[i])
         return false;
   }
   return true;
}
} // namespace

bool Set(Interface interface_type, IUnknown* object, std::span<const InterfaceVersion> versions)
{
   const std::string_view name = versions.front().name;
   void* const* vtable = GetVtable(object);
   {
      std::scoped_lock lock(mutex);
      const Entry& entry = entries[size_t(interface_type)];
      if (!entry.methods.empty())
      {
         if (SameMethods(entry.methods, vtable))
            return true;
         Utils::Log::Warning("Vtables: {} at {} in {} has different methods from the first one, its functions may not be hooked",
            name, static_cast<const void*>(vtable), Utils::Modules::GetName(vtable));
         return false;
      }
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
      if (!entry.methods.empty())
         return SameMethods(entry.methods, vtable);
      entry = {std::move(methods), std::move(supported_versions)};
   }
   // The vtable may be in heap memory (D3D11 contexts), its methods never are
   Utils::Log::Info("Vtables: {} ({}) at {} in {}, methods in {}", name, highest_version, static_cast<const void*>(vtable),
      Utils::Modules::GetName(vtable), Utils::Modules::GetName(vtable[3]));
   return true;
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
