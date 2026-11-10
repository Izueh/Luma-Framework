// The loader resolves each export to the system dxgi.dll, so Luma doesn't need its own stubs.
// "dxgi.X" would resolve to Luma itself, hence the full path. 32 bit processes get SysWOW64, the loader doesn't redirect NT paths.
#ifdef _WIN64
#define LUMA_DXGI_PATH "\\\\.\\GLOBALROOT\\SystemRoot\\System32\\dxgi.dll."
#else
#define LUMA_DXGI_PATH "\\\\.\\GLOBALROOT\\SystemRoot\\SysWOW64\\dxgi.dll."
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
LUMA_DXGI_EXPORT(CreateDXGIFactory, 10)
LUMA_DXGI_EXPORT(CreateDXGIFactory1, 11)
LUMA_DXGI_EXPORT(CreateDXGIFactory2, 12)
LUMA_DXGI_EXPORT(DXGID3D10CreateDevice, 13)
LUMA_DXGI_EXPORT(DXGID3D10CreateLayeredDevice, 14)
LUMA_DXGI_EXPORT(DXGID3D10GetLayeredDeviceSize, 15)
LUMA_DXGI_EXPORT(DXGID3D10RegisterLayers, 16)
LUMA_DXGI_EXPORT(DXGIDeclareAdapterRemovalSupport, 17)
LUMA_DXGI_EXPORT(DXGIDisableVBlankVirtualization, 18)
LUMA_DXGI_EXPORT(DXGIGetDebugInterface1, 19)
LUMA_DXGI_EXPORT(DXGIReportAdapterConfiguration, 20)

// Nothing references this file, so the game links this symbol (see "Luma.Features.props") to keep the exports in
extern "C" void LumaDllExports_dxgi() {}
