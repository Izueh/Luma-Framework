#pragma once

#include <stdint.h>

#define LUMA_SLOTS(SLOT)                                                  \
   SLOT(ID3D11DeviceChild, GetDevice)                                     \
   SLOT(ID3D11Device, GetFeatureLevel)                                    \
   SLOT(ID3D11Device, CreateBuffer)                                       \
   SLOT(ID3D11Device, CreateTexture1D)                                    \
   SLOT(ID3D11Device, CreateTexture2D)                                    \
   SLOT(ID3D11Device, CreateTexture3D)                                    \
   SLOT(ID3D11Device, CreateShaderResourceView)                           \
   SLOT(ID3D11Device, CreateUnorderedAccessView)                          \
   SLOT(ID3D11Device, CreateRenderTargetView)                             \
   SLOT(ID3D11Device, CreateDepthStencilView)                             \
   SLOT(ID3D11Device, CreateInputLayout)                                  \
   SLOT(ID3D11Device, CreateVertexShader)                                 \
   SLOT(ID3D11Device, CreateGeometryShader)                               \
   SLOT(ID3D11Device, CreateGeometryShaderWithStreamOutput)               \
   SLOT(ID3D11Device, CreatePixelShader)                                  \
   SLOT(ID3D11Device, CreateHullShader)                                   \
   SLOT(ID3D11Device, CreateDomainShader)                                 \
   SLOT(ID3D11Device, CreateComputeShader)                                \
   SLOT(ID3D11Device, CreateSamplerState)                                 \
   SLOT(ID3D11Device, CreateBlendState)                                   \
   SLOT(ID3D11Device1, CreateBlendState1)                                 \
   SLOT(ID3D11Device, CreateDeferredContext)                              \
   SLOT(ID3D11Device, OpenSharedResource)                                 \
   SLOT(ID3D11Device1, CreateDeferredContext1)                            \
   SLOT(ID3D11Device1, OpenSharedResource1)                               \
   SLOT(ID3D11Device1, OpenSharedResourceByName)                          \
   SLOT(ID3D11Device2, CreateDeferredContext2)                            \
   SLOT(ID3D11Device3, CreateTexture2D1)                                  \
   SLOT(ID3D11Device3, CreateTexture3D1)                                  \
   SLOT(ID3D11Device3, CreateShaderResourceView1)                         \
   SLOT(ID3D11Device3, CreateUnorderedAccessView1)                        \
   SLOT(ID3D11Device3, CreateRenderTargetView1)                           \
   SLOT(ID3D11Device3, CreateDeferredContext3)                            \
   SLOT(ID3D11DeviceContext, GetDevice)                                   \
   SLOT(ID3D11DeviceContext, VSSetConstantBuffers)                        \
   SLOT(ID3D11DeviceContext, PSSetShaderResources)                        \
   SLOT(ID3D11DeviceContext, PSSetShader)                                 \
   SLOT(ID3D11DeviceContext, PSSetSamplers)                               \
   SLOT(ID3D11DeviceContext, VSSetShader)                                 \
   SLOT(ID3D11DeviceContext, DrawIndexed)                                 \
   SLOT(ID3D11DeviceContext, Draw)                                        \
   SLOT(ID3D11DeviceContext, Map)                                         \
   SLOT(ID3D11DeviceContext, Unmap)                                       \
   SLOT(ID3D11DeviceContext, PSSetConstantBuffers)                        \
   SLOT(ID3D11DeviceContext, DrawIndexedInstanced)                        \
   SLOT(ID3D11DeviceContext, DrawInstanced)                               \
   SLOT(ID3D11DeviceContext, GSSetConstantBuffers)                        \
   SLOT(ID3D11DeviceContext, GSSetShader)                                 \
   SLOT(ID3D11DeviceContext, VSSetShaderResources)                        \
   SLOT(ID3D11DeviceContext, VSSetSamplers)                               \
   SLOT(ID3D11DeviceContext, GSSetShaderResources)                        \
   SLOT(ID3D11DeviceContext, GSSetSamplers)                               \
   SLOT(ID3D11DeviceContext, OMSetRenderTargets)                          \
   SLOT(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews)   \
   SLOT(ID3D11DeviceContext, DrawAuto)                                    \
   SLOT(ID3D11DeviceContext, DrawIndexedInstancedIndirect)                \
   SLOT(ID3D11DeviceContext, DrawInstancedIndirect)                       \
   SLOT(ID3D11DeviceContext, Dispatch)                                    \
   SLOT(ID3D11DeviceContext, DispatchIndirect)                            \
   SLOT(ID3D11DeviceContext, RSSetViewports)                              \
   SLOT(ID3D11DeviceContext, RSSetScissorRects)                           \
   SLOT(ID3D11DeviceContext, CopySubresourceRegion)                       \
   SLOT(ID3D11DeviceContext, CopyResource)                                \
   SLOT(ID3D11DeviceContext, UpdateSubresource)                           \
   SLOT(ID3D11DeviceContext, ClearRenderTargetView)                       \
   SLOT(ID3D11DeviceContext, ClearUnorderedAccessViewUint)                \
   SLOT(ID3D11DeviceContext, ClearUnorderedAccessViewFloat)               \
   SLOT(ID3D11DeviceContext, ClearDepthStencilView)                       \
   SLOT(ID3D11DeviceContext, ResolveSubresource)                          \
   SLOT(ID3D11DeviceContext, ExecuteCommandList)                          \
   SLOT(ID3D11DeviceContext, HSSetShaderResources)                        \
   SLOT(ID3D11DeviceContext, HSSetShader)                                 \
   SLOT(ID3D11DeviceContext, HSSetSamplers)                               \
   SLOT(ID3D11DeviceContext, HSSetConstantBuffers)                        \
   SLOT(ID3D11DeviceContext, DSSetShaderResources)                        \
   SLOT(ID3D11DeviceContext, DSSetShader)                                 \
   SLOT(ID3D11DeviceContext, DSSetSamplers)                               \
   SLOT(ID3D11DeviceContext, DSSetConstantBuffers)                        \
   SLOT(ID3D11DeviceContext, CSSetShaderResources)                        \
   SLOT(ID3D11DeviceContext, CSSetUnorderedAccessViews)                   \
   SLOT(ID3D11DeviceContext, CSSetShader)                                 \
   SLOT(ID3D11DeviceContext, CSSetSamplers)                               \
   SLOT(ID3D11DeviceContext, CSSetConstantBuffers)                        \
   SLOT(ID3D11DeviceContext, ClearState)                                  \
   SLOT(ID3D11DeviceContext, Flush)                                       \
   SLOT(ID3D11DeviceContext, FinishCommandList)                           \
   SLOT(ID3D11DeviceContext1, CopySubresourceRegion1)                     \
   SLOT(ID3D11DeviceContext1, UpdateSubresource1)                         \
   SLOT(ID3D11DeviceContext1, VSSetConstantBuffers1)                      \
   SLOT(ID3D11DeviceContext1, HSSetConstantBuffers1)                      \
   SLOT(ID3D11DeviceContext1, DSSetConstantBuffers1)                      \
   SLOT(ID3D11DeviceContext1, GSSetConstantBuffers1)                      \
   SLOT(ID3D11DeviceContext1, PSSetConstantBuffers1)                      \
   SLOT(ID3D11DeviceContext1, CSSetConstantBuffers1)                      \
   SLOT(ID3D11DeviceContext1, ClearView)                                  \
   SLOT(IDXGIFactory, CreateSwapChain)                                    \
   SLOT(IDXGIFactory2, CreateSwapChainForHwnd)                            \
   SLOT(IDXGISwapChain, GetDevice)                                        \
   SLOT(IDXGISwapChain, Present)                                          \
   SLOT(IDXGISwapChain, SetFullscreenState)                               \
   SLOT(IDXGISwapChain, ResizeBuffers)                                    \
   SLOT(IDXGISwapChain1, Present1)                                        \
   SLOT(IDXGISwapChain3, ResizeBuffers1)

// The interface versions whose vtables Luma records (see "LUMA_INTERFACE_VERSION")
#define LUMA_VTABLE_INTERFACES(INTERFACE) \
   INTERFACE(IDXGIFactory)                \
   INTERFACE(IDXGIFactory1)               \
   INTERFACE(IDXGIFactory2)               \
   INTERFACE(IDXGIFactory3)               \
   INTERFACE(IDXGIFactory4)               \
   INTERFACE(IDXGIFactory5)               \
   INTERFACE(IDXGIFactory6)               \
   INTERFACE(IDXGIFactory7)               \
   INTERFACE(IDXGISwapChain)              \
   INTERFACE(IDXGISwapChain1)             \
   INTERFACE(IDXGISwapChain2)             \
   INTERFACE(IDXGISwapChain3)             \
   INTERFACE(IDXGISwapChain4)             \
   INTERFACE(ID3D11Device)                \
   INTERFACE(ID3D11Device1)               \
   INTERFACE(ID3D11Device2)               \
   INTERFACE(ID3D11Device3)               \
   INTERFACE(ID3D11Device4)               \
   INTERFACE(ID3D11Device5)               \
   INTERFACE(ID3D11DeviceContext)         \
   INTERFACE(ID3D11DeviceContext1)        \
   INTERFACE(ID3D11DeviceContext2)        \
   INTERFACE(ID3D11DeviceContext3)        \
   INTERFACE(ID3D11DeviceContext4)        \
   INTERFACE(ID3D11DeviceChild)

#ifdef __cplusplus
extern "C"
{
#endif

#define LUMA_DECLARE_SLOT(Interface, Method) extern const uint32_t LumaSlot_##Interface##_##Method;
   LUMA_SLOTS(LUMA_DECLARE_SLOT)
#undef LUMA_DECLARE_SLOT

#define LUMA_DECLARE_METHOD_COUNT(Interface) extern const uint32_t LumaMethodCount_##Interface;
   LUMA_VTABLE_INTERFACES(LUMA_DECLARE_METHOD_COUNT)
#undef LUMA_DECLARE_METHOD_COUNT

#ifdef __cplusplus
}
#endif
