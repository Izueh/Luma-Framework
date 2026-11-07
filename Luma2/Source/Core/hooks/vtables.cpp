#include "hooks/vtables.hpp"

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
   void* const* vtable = nullptr;
   std::vector<IID> supported_versions;
};

std::mutex mutex;
std::array<Entry, size_t(Interface::Count)> entries;

void* const* GetVtable(IUnknown* object)
{
   return *reinterpret_cast<void* const* const*>(object);
}
} // namespace

bool Set(Interface interface_type, IUnknown* object, std::span<const InterfaceVersion> versions)
{
   const std::string_view name = versions.front().name;
   void* const* vtable = GetVtable(object);
   void* const* recorded;
   {
      std::scoped_lock lock(mutex);
      recorded = entries[size_t(interface_type)].vtable;
   }
   if (recorded == vtable)
      return true;
   if (recorded)
   {
      Utils::Log::Warning("Vtables: {} at {} in {} differs from the first one at {}, its functions may not be hooked",
         name, static_cast<const void*>(vtable), Utils::Modules::GetName(vtable), static_cast<const void*>(recorded));
      return false;
   }

   // Without the lock: this calls into the API
   std::vector<IID> supported_versions;
   std::string_view highest_version = "unknown";
   for (const InterfaceVersion& version : versions)
   {
      IUnknown* versioned = nullptr;
      if (SUCCEEDED(object->QueryInterface(version.iid, reinterpret_cast<void**>(&versioned))) && versioned)
      {
         versioned->Release();
         supported_versions.push_back(version.iid);
         highest_version = version.name;
      }
   }
   {
      std::scoped_lock lock(mutex);
      Entry& entry = entries[size_t(interface_type)];
      if (entry.vtable)
         return entry.vtable == vtable;
      entry = {vtable, std::move(supported_versions)};
   }
   // The vtable may be in heap memory (D3D11 contexts), its methods never are
   Utils::Log::Info("Vtables: {} ({}) at {} in {}, methods in {}", name, highest_version, static_cast<const void*>(vtable),
      Utils::Modules::GetName(vtable), Utils::Modules::GetName(vtable[3]));
   return true;
}

void* const* Get(Interface interface_type)
{
   std::scoped_lock lock(mutex);
   return entries[size_t(interface_type)].vtable;
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
