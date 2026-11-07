#pragma once

#include <stdint.h>

#define LUMA_SLOTS(SLOT)                       \
   SLOT(ID3D11DeviceChild, GetDevice)          \
   SLOT(ID3D11DeviceContext, GetDevice)        \
   SLOT(ID3D12DeviceChild, GetDevice)          \
   SLOT(IDXGIFactory, CreateSwapChain)         \
   SLOT(IDXGIFactory2, CreateSwapChainForHwnd) \
   SLOT(IDXGISwapChain, GetDevice)             \
   SLOT(IDXGISwapChain, Present)               \
   SLOT(IDXGISwapChain, SetFullscreenState)    \
   SLOT(IDXGISwapChain, ResizeBuffers)         \
   SLOT(IDXGISwapChain1, Present1)             \
   SLOT(IDXGISwapChain3, ResizeBuffers1)

#ifdef __cplusplus
extern "C"
{
#endif

#define LUMA_DECLARE_SLOT(Interface, Method) extern const uint32_t LumaSlot_##Interface##_##Method;
   LUMA_SLOTS(LUMA_DECLARE_SLOT)
#undef LUMA_DECLARE_SLOT

#ifdef __cplusplus
}
#endif
