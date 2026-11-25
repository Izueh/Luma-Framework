#include "Effects/SR/DXGI/DXGIFidelityFXSpoofing.hpp"

#include <atomic>
#include <cwchar>
#include <mutex>
#include <vector>

#include <dxgi1_6.h>
#include <intrin.h>
#include <wrl/client.h>

#include "Hooks/HookManager.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace DXGI {
namespace FidelityFXSpoofing {
namespace {
constexpr UINT VENDOR_ID_AMD = 0x1002;
constexpr UINT AMD_PRIVATE_DATA_SIZE = 608;
constexpr size_t AMD_ASIC_FAMILY_INDEX = 12;
constexpr size_t AMD_ASIC_REVISION_INDEX = 13;
constexpr UINT AMD_ASIC_FAMILY_RDNA3 = 145;
constexpr UINT AMD_ASIC_REVISION_RDNA3 = 1;
constexpr const wchar_t* AMD_DRIVER_MODULE_NAME = L"amdxc64.dll";
constexpr UINT KMTQAITYPE_UMDRIVERPRIVATE = 0;

struct D3dkmtQueryAdapterInfo
{
   UINT adapter;
   UINT type;
   void* private_driver_data;
   UINT private_driver_data_size;
};

using GetDescFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter*, DXGI_ADAPTER_DESC*);
using GetDesc1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter1*, DXGI_ADAPTER_DESC1*);
using GetDesc2Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter2*, DXGI_ADAPTER_DESC2*);
using GetDesc3Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter4*, DXGI_ADAPTER_DESC3*);
using QueryAdapterInfoFn = LONG(APIENTRY*)(const D3dkmtQueryAdapterInfo*);
using GetModuleHandleAFn = HMODULE(WINAPI*)(LPCSTR);
using GetModuleHandleWFn = HMODULE(WINAPI*)(LPCWSTR);
using GetModuleHandleExWFn = BOOL(WINAPI*)(DWORD, LPCWSTR, HMODULE*);

std::mutex init_mutex;
std::mutex active_mutex;
bool initialized = false;
std::atomic<uintptr_t> target_begin = 0;
std::atomic<uintptr_t> target_end = 0;

GetDescFn orig_get_desc = nullptr;
GetDesc1Fn orig_get_desc1 = nullptr;
GetDesc2Fn orig_get_desc2 = nullptr;
GetDesc3Fn orig_get_desc3 = nullptr;
QueryAdapterInfoFn orig_query_adapter_info = nullptr;
GetModuleHandleAFn orig_get_module_handle_a = nullptr;
GetModuleHandleWFn orig_get_module_handle_w = nullptr;
GetModuleHandleExWFn orig_get_module_handle_ex_w = nullptr;

bool IsTargetCaller(const void* caller)
{
   const uintptr_t begin = target_begin.load(std::memory_order_acquire);
   if (begin == 0 || !caller)
      return false;
   const auto address = reinterpret_cast<uintptr_t>(caller);
   return address >= begin && address < target_end.load(std::memory_order_relaxed);
}

bool MatchesHiddenModule(LPCWSTR name)
{
   if (!name)
      return false;
   const wchar_t* file = wcsrchr(name, L'\\');
   if (!file)
      file = wcsrchr(name, L'/');
   file = file ? file + 1 : name;
   return _wcsicmp(file, AMD_DRIVER_MODULE_NAME) == 0;
}

HRESULT STDMETHODCALLTYPE HookGetDesc(IDXGIAdapter* self, DXGI_ADAPTER_DESC* desc)
{
   const HRESULT hr = orig_get_desc(self, desc);
   if (SUCCEEDED(hr) && desc && IsTargetCaller(_ReturnAddress()))
      desc->VendorId = VENDOR_ID_AMD;
   return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDesc1(IDXGIAdapter1* self, DXGI_ADAPTER_DESC1* desc)
{
   const HRESULT hr = orig_get_desc1(self, desc);
   if (SUCCEEDED(hr) && desc && IsTargetCaller(_ReturnAddress()))
      desc->VendorId = VENDOR_ID_AMD;
   return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDesc2(IDXGIAdapter2* self, DXGI_ADAPTER_DESC2* desc)
{
   const HRESULT hr = orig_get_desc2(self, desc);
   if (SUCCEEDED(hr) && desc && IsTargetCaller(_ReturnAddress()))
      desc->VendorId = VENDOR_ID_AMD;
   return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDesc3(IDXGIAdapter4* self, DXGI_ADAPTER_DESC3* desc)
{
   const HRESULT hr = orig_get_desc3(self, desc);
   if (SUCCEEDED(hr) && desc && IsTargetCaller(_ReturnAddress()))
      desc->VendorId = VENDOR_ID_AMD;
   return hr;
}

LONG APIENTRY HookQueryAdapterInfo(const D3dkmtQueryAdapterInfo* info)
{
   const LONG status = orig_query_adapter_info(info);
   if (info && info->type == KMTQAITYPE_UMDRIVERPRIVATE && info->private_driver_data && info->private_driver_data_size == AMD_PRIVATE_DATA_SIZE &&
      IsTargetCaller(_ReturnAddress()))
   {
      auto* amd_gpu_info = static_cast<UINT*>(info->private_driver_data);
      amd_gpu_info[AMD_ASIC_FAMILY_INDEX] = AMD_ASIC_FAMILY_RDNA3;
      amd_gpu_info[AMD_ASIC_REVISION_INDEX] = AMD_ASIC_REVISION_RDNA3;
      return 1;
   }
   return status;
}

HMODULE WINAPI HookGetModuleHandleA(LPCSTR module_name)
{
   if (module_name && IsTargetCaller(_ReturnAddress()))
   {
      wchar_t wide[MAX_PATH] = {};
      if (MultiByteToWideChar(CP_ACP, 0, module_name, -1, wide, MAX_PATH) && MatchesHiddenModule(wide))
      {
         SetLastError(ERROR_MOD_NOT_FOUND);
         return nullptr;
      }
   }
   return orig_get_module_handle_a(module_name);
}

HMODULE WINAPI HookGetModuleHandleW(LPCWSTR module_name)
{
   if (MatchesHiddenModule(module_name) && IsTargetCaller(_ReturnAddress()))
   {
      SetLastError(ERROR_MOD_NOT_FOUND);
      return nullptr;
   }
   return orig_get_module_handle_w(module_name);
}

BOOL WINAPI HookGetModuleHandleExW(DWORD flags, LPCWSTR module_name, HMODULE* out_module)
{
   if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) == 0 && MatchesHiddenModule(module_name) && IsTargetCaller(_ReturnAddress()))
   {
      if (out_module)
         *out_module = nullptr;
      return FALSE;
   }
   return orig_get_module_handle_ex_w(flags, module_name, out_module);
}
} // namespace

bool Init(IDXGIAdapter* adapter)
{
   std::scoped_lock lock(init_mutex);
   if (initialized)
      return true;
   if (!adapter)
      return false;

   auto* vtable = *reinterpret_cast<void* const* const*>(adapter);
   std::vector<Hooks::Hook> hooks;
   hooks.push_back({Hooks::AddressTarget{vtable[8], "IDXGIAdapter::GetDesc (FFX spoof)"}, reinterpret_cast<void*>(&HookGetDesc),
      reinterpret_cast<void**>(&orig_get_desc)});
   if (ComPtr<IDXGIAdapter1> adapter1; SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter1))))
   {
      auto* vt1 = *reinterpret_cast<void* const* const*>(adapter1.Get());
      hooks.push_back({Hooks::AddressTarget{vt1[10], "IDXGIAdapter1::GetDesc1 (FFX spoof)"}, reinterpret_cast<void*>(&HookGetDesc1),
         reinterpret_cast<void**>(&orig_get_desc1)});
   }
   if (ComPtr<IDXGIAdapter2> adapter2; SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter2))))
   {
      auto* vt2 = *reinterpret_cast<void* const* const*>(adapter2.Get());
      hooks.push_back({Hooks::AddressTarget{vt2[11], "IDXGIAdapter2::GetDesc2 (FFX spoof)"}, reinterpret_cast<void*>(&HookGetDesc2),
         reinterpret_cast<void**>(&orig_get_desc2)});
   }
   if (ComPtr<IDXGIAdapter4> adapter4; SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter4))))
   {
      auto* vt4 = *reinterpret_cast<void* const* const*>(adapter4.Get());
      hooks.push_back({Hooks::AddressTarget{vt4[18], "IDXGIAdapter4::GetDesc3 (FFX spoof)"}, reinterpret_cast<void*>(&HookGetDesc3),
         reinterpret_cast<void**>(&orig_get_desc3)});
   }
   // Resolve via GetProcAddress so forwarded exports (gdi32.dll -> win32u.dll) resolve to the real entry point
   if (HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll"))
   {
      if (void* kmt_addr = reinterpret_cast<void*>(GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo")))
      {
         hooks.push_back({Hooks::AddressTarget{kmt_addr, "D3DKMTQueryAdapterInfo (FFX spoof)"}, reinterpret_cast<void*>(&HookQueryAdapterInfo),
            reinterpret_cast<void**>(&orig_query_adapter_info)});
      }
   }
   if (HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll"))
   {
      if (void* gmh_a = reinterpret_cast<void*>(GetProcAddress(kernel32, "GetModuleHandleA")))
      {
         hooks.push_back({Hooks::AddressTarget{gmh_a, "GetModuleHandleA (FFX spoof)"}, reinterpret_cast<void*>(&HookGetModuleHandleA),
            reinterpret_cast<void**>(&orig_get_module_handle_a)});
      }
   }
   hooks.push_back({Hooks::ExportTarget{L"kernelbase.dll", "GetModuleHandleW"}, reinterpret_cast<void*>(&HookGetModuleHandleW),
      reinterpret_cast<void**>(&orig_get_module_handle_w)});
   hooks.push_back({Hooks::ExportTarget{L"kernelbase.dll", "GetModuleHandleExW"}, reinterpret_cast<void*>(&HookGetModuleHandleExW),
      reinterpret_cast<void**>(&orig_get_module_handle_ex_w)});

   Hooks::Declare(hooks);
   Hooks::InstallPending();
   initialized = orig_get_desc != nullptr && orig_query_adapter_info != nullptr;
   if (!initialized)
      Log::Warning("SR: failed to install AMD spoofing hooks for FSR 4");
   return initialized;
}

bool Begin(HMODULE ffx_module)
{
   active_mutex.lock();
   if (!initialized || !ffx_module)
   {
      active_mutex.unlock();
      return false;
   }
   const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ffx_module);
   const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(ffx_module) + dos->e_lfanew);
   const auto begin = reinterpret_cast<uintptr_t>(ffx_module);
   target_end.store(begin + nt->OptionalHeader.SizeOfImage, std::memory_order_relaxed);
   target_begin.store(begin, std::memory_order_release);
   return true;
}

void End()
{
   target_begin.store(0, std::memory_order_release);
   target_end.store(0, std::memory_order_relaxed);
   active_mutex.unlock();
}
} // namespace FidelityFXSpoofing
} // namespace DXGI
} // namespace SR
} // namespace Effects
} // namespace Luma
