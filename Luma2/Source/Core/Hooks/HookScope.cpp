#include "Hooks/HookScope.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include <Windows.h>

namespace Luma {
namespace Hooks {
namespace Scope {
namespace {
constexpr size_t MAX_IGNORED_MODULES = 16;

struct ModuleRange
{
   std::atomic<uintptr_t> begin = 0;
   std::atomic<uintptr_t> end = 0;
};

thread_local uint32_t depth = 0;
thread_local bool third_party_work = false;
std::atomic<bool> bypass = false;
std::atomic<CallerFilter> caller_filter = nullptr;
std::mutex ignored_modules_mutex;
std::array<ModuleRange, MAX_IGNORED_MODULES> ignored_modules = {};
std::atomic<uint32_t> ignored_module_count = 0;

bool GetModuleImageRange(void* module_handle, uintptr_t& begin, uintptr_t& end)
{
   if (!module_handle)
      return false;
   const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(module_handle);
   if (dos->e_magic != IMAGE_DOS_SIGNATURE)
      return false;
   const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(static_cast<const uint8_t*>(module_handle) + dos->e_lfanew);
   if (nt->Signature != IMAGE_NT_SIGNATURE)
      return false;
   begin = reinterpret_cast<uintptr_t>(module_handle);
   end = begin + nt->OptionalHeader.SizeOfImage;
   return end > begin;
}
} // namespace

Guard::Guard()
{
   depth++;
}

Guard::~Guard()
{
   depth--;
}

Suspension::Suspension() : depth(Scope::depth)
{
   Scope::depth = 0;
}

Suspension::~Suspension()
{
   Scope::depth = depth;
}

bool IsActive()
{
   return depth != 0 || bypass.load(std::memory_order_relaxed);
}

void SetBypass(bool value)
{
   bypass.store(value, std::memory_order_seq_cst);
}

bool IsBypassed()
{
   return bypass.load(std::memory_order_relaxed);
}

// One level of the scope's depth, so IsActive stays one check on the hot paths
void BeginThirdPartyWork()
{
   if (third_party_work)
      return;
   third_party_work = true;
   depth++;
}

void EndThirdPartyWork()
{
   if (!third_party_work)
      return;
   third_party_work = false;
   depth--;
}

bool IsThirdPartyWorkOpen()
{
   return third_party_work;
}

void SetCallerFilter(CallerFilter filter)
{
   caller_filter.store(filter, std::memory_order_release);
}

void AddIgnoredModule(void* module_handle)
{
   uintptr_t begin = 0;
   uintptr_t end = 0;
   if (!GetModuleImageRange(module_handle, begin, end))
      return;
   std::scoped_lock lock(ignored_modules_mutex);
   const uint32_t count = ignored_module_count.load(std::memory_order_relaxed);
   for (uint32_t i = 0; i < count; i++)
   {
      if (ignored_modules[i].begin.load(std::memory_order_relaxed) == begin)
         return;
   }
   for (uint32_t i = 0; i < count; i++)
   {
      if (ignored_modules[i].begin.load(std::memory_order_relaxed) == 0)
      {
         ignored_modules[i].end.store(end, std::memory_order_relaxed);
         ignored_modules[i].begin.store(begin, std::memory_order_release);
         return;
      }
   }
   if (count < MAX_IGNORED_MODULES)
   {
      ignored_modules[count].end.store(end, std::memory_order_relaxed);
      ignored_modules[count].begin.store(begin, std::memory_order_release);
      ignored_module_count.store(count + 1, std::memory_order_release);
   }
}

void RemoveIgnoredModule(void* module_handle)
{
   const auto begin = reinterpret_cast<uintptr_t>(module_handle);
   if (begin == 0)
      return;
   std::scoped_lock lock(ignored_modules_mutex);
   const uint32_t count = ignored_module_count.load(std::memory_order_relaxed);
   for (uint32_t i = 0; i < count; i++)
   {
      if (ignored_modules[i].begin.load(std::memory_order_relaxed) == begin)
      {
         ignored_modules[i].begin.store(0, std::memory_order_release);
         ignored_modules[i].end.store(0, std::memory_order_relaxed);
         return;
      }
   }
}

bool IsIgnoredCaller(const void* caller)
{
   if (!caller)
      return false;
   const uint32_t count = ignored_module_count.load(std::memory_order_acquire);
   if (count > 0)
   {
      const auto address = reinterpret_cast<uintptr_t>(caller);
      for (uint32_t i = 0; i < count; i++)
      {
         const uintptr_t begin = ignored_modules[i].begin.load(std::memory_order_acquire);
         if (begin != 0 && address >= begin && address < ignored_modules[i].end.load(std::memory_order_relaxed))
            return true;
      }
   }
   const CallerFilter filter = caller_filter.load(std::memory_order_acquire);
   return filter && filter(caller);
}
} // namespace Scope
} // namespace Hooks
} // namespace Luma
