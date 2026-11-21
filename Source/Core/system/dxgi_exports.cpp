#include <Windows.h>

#include <dxgi1_6.h>

#include "utils/modules.hpp"

// Most exports are forwarders the loader resolves to the system dxgi.dll, so Luma doesn't need its own stubs.
// "dxgi.X" would resolve to Luma itself, hence the full path. 32 bit processes get SysWOW64, the loader doesn't redirect NT paths.
// Wine can't open that path: the exports Wine's or DXVK's dxgi.dll have are Luma's own functions, calling the system dxgi.dll.
// The others only exist on Windows.
// On 32 bit, a pragma's export doesn't find a decorated symbol ("_Name@ArgumentsSize") by itself.
#ifdef _WIN64
#define LUMA_DXGI_PATH "\\\\.\\GLOBALROOT\\SystemRoot\\System32\\dxgi.dll."
#define LUMA_DXGI_FUNCTION(name, ordinal, arguments_size) __pragma(comment(linker, "/EXPORT:" #name ",@" #ordinal))
#else
#define LUMA_DXGI_PATH "\\\\.\\GLOBALROOT\\SystemRoot\\SysWOW64\\dxgi.dll."
#define LUMA_DXGI_FUNCTION(name, ordinal, arguments_size) __pragma(comment(linker, "/EXPORT:" #name "=_" #name "@" #arguments_size ",@" #ordinal))
#endif
#define LUMA_DXGI_EXPORT(name, ordinal) __pragma(comment(linker, "/EXPORT:" #name "=" LUMA_DXGI_PATH #name ",@" #ordinal))

// The names and ordinals of the system dxgi.dll ("dumpbin /exports C:\Windows\System32\dxgi.dll")
LUMA_DXGI_EXPORT(ApplyCompatResolutionQuirking, 1)
LUMA_DXGI_EXPORT(CompatString, 2)
LUMA_DXGI_EXPORT(CompatValue, 3)
LUMA_DXGI_EXPORT(DXGIDumpJournal, 4)
LUMA_DXGI_EXPORT(PIXBeginCapture, 5)
LUMA_DXGI_EXPORT(PIXEndCapture, 6)
LUMA_DXGI_EXPORT(PIXGetCaptureState, 7)
LUMA_DXGI_EXPORT(SetAppCompatStringPointer, 8)
LUMA_DXGI_EXPORT(UpdateHMDEmulationStatus, 9)
LUMA_DXGI_FUNCTION(CreateDXGIFactory, 10, 8)
LUMA_DXGI_FUNCTION(CreateDXGIFactory1, 11, 8)
LUMA_DXGI_FUNCTION(CreateDXGIFactory2, 12, 12)
LUMA_DXGI_FUNCTION(DXGID3D10CreateDevice, 13, 28)
LUMA_DXGI_EXPORT(DXGID3D10CreateLayeredDevice, 14)
LUMA_DXGI_EXPORT(DXGID3D10GetLayeredDeviceSize, 15)
LUMA_DXGI_FUNCTION(DXGID3D10RegisterLayers, 16, 8)
LUMA_DXGI_FUNCTION(DXGIDeclareAdapterRemovalSupport, 17, 0)
LUMA_DXGI_EXPORT(DXGIDisableVBlankVirtualization, 18)
LUMA_DXGI_FUNCTION(DXGIGetDebugInterface1, 19, 12)
LUMA_DXGI_EXPORT(DXGIReportAdapterConfiguration, 20)

namespace
{
   // Loads the system dxgi.dll if it isn't loaded yet. Not GetProcAddress (see FindExport).
   void* FindSystemExport(const char* name)
   {
      HMODULE module = Luma::Utils::Modules::FindSystemModule(L"dxgi.dll");
      if (!module)
         module = LoadLibraryW(Luma::Utils::Modules::SystemPath(L"dxgi.dll").c_str());
      return Luma::Utils::Modules::FindExport(module, name);
   }
} // namespace

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** factory)
{
   const auto function = reinterpret_cast<decltype(&CreateDXGIFactory)>(FindSystemExport("CreateDXGIFactory"));
   if (!function)
      return E_FAIL;
   return function(riid, factory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory)
{
   const auto function = reinterpret_cast<decltype(&CreateDXGIFactory1)>(FindSystemExport("CreateDXGIFactory1"));
   if (!function)
      return E_FAIL;
   return function(riid, factory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** factory)
{
   const auto function = reinterpret_cast<decltype(&CreateDXGIFactory2)>(FindSystemExport("CreateDXGIFactory2"));
   if (!function)
      return E_FAIL;
   return function(flags, riid, factory);
}

// Not in the Windows SDK: Wine's own d3d11.dll and d3d10core.dll import them (Wine without DXVK). The signatures are Wine's and
// ReShade's.
extern "C" HRESULT WINAPI DXGID3D10CreateDevice(HMODULE module, IDXGIFactory* factory, IDXGIAdapter* adapter, UINT flags, const void* feature_levels, UINT feature_level_count, void** device)
{
   const auto function = reinterpret_cast<decltype(&DXGID3D10CreateDevice)>(FindSystemExport("DXGID3D10CreateDevice"));
   if (!function)
      return E_FAIL;
   return function(module, factory, adapter, flags, feature_levels, feature_level_count, device);
}

extern "C" HRESULT WINAPI DXGID3D10RegisterLayers(const void* layers, UINT layer_count)
{
   const auto function = reinterpret_cast<decltype(&DXGID3D10RegisterLayers)>(FindSystemExport("DXGID3D10RegisterLayers"));
   if (!function)
      return E_FAIL;
   return function(layers, layer_count);
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport()
{
   const auto function = reinterpret_cast<decltype(&DXGIDeclareAdapterRemovalSupport)>(FindSystemExport("DXGIDeclareAdapterRemovalSupport"));
   if (!function)
      return E_FAIL;
   return function();
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void** debug)
{
   const auto function = reinterpret_cast<decltype(&DXGIGetDebugInterface1)>(FindSystemExport("DXGIGetDebugInterface1"));
   if (!function)
      return E_FAIL;
   return function(flags, riid, debug);
}

// Nothing references this file, so the game links this symbol (see "Luma.Features.props") to keep the exports in
extern "C" void LumaDllExports_dxgi()
{
}
