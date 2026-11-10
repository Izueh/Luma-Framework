#pragma once

#include <bitset>
#include <cstdint>

namespace Luma {
namespace Hooks {
namespace D3D11 {
// The native functions whose hooks Core or the game need. A versioned variant (UpdateSubresource1, ResizeBuffers1...) is
// hooked with its base function.
enum class Function : uint32_t
{
   // IDXGISwapChain
   Present,
   ResizeBuffers,
   SetFullscreenState,

   // ID3D11Device
   CreateBuffer,
   CreateTexture1D,
   CreateTexture2D,
   CreateTexture3D,
   OpenSharedResource,
   CreateShaderResourceView,
   CreateRenderTargetView,
   CreateUnorderedAccessView,
   CreateDepthStencilView,
   CreateSamplerState,
   CreateBlendState,
   CreateInputLayout,
   CreateVertexShader,
   CreateHullShader,
   CreateDomainShader,
   CreateGeometryShader,
   CreateGeometryShaderWithStreamOutput,
   CreatePixelShader,
   CreateComputeShader,
   CreateDeferredContext,

   // ID3D11DeviceContext
   VSSetShader,
   HSSetShader,
   DSSetShader,
   GSSetShader,
   PSSetShader,
   CSSetShader,
   VSSetShaderResources,
   HSSetShaderResources,
   DSSetShaderResources,
   GSSetShaderResources,
   PSSetShaderResources,
   CSSetShaderResources,
   VSSetConstantBuffers,
   HSSetConstantBuffers,
   DSSetConstantBuffers,
   GSSetConstantBuffers,
   PSSetConstantBuffers,
   CSSetConstantBuffers,
   VSSetSamplers,
   HSSetSamplers,
   DSSetSamplers,
   GSSetSamplers,
   PSSetSamplers,
   CSSetSamplers,
   CSSetUnorderedAccessViews,
   OMSetRenderTargets,
   OMSetRenderTargetsAndUnorderedAccessViews,
   RSSetViewports,
   RSSetScissorRects,
   ClearState,
   Map,
   Unmap,
   UpdateSubresource,
   CopyResource,
   CopySubresourceRegion,
   ResolveSubresource,
   ClearRenderTargetView,
   ClearDepthStencilView,
   ClearUnorderedAccessViewUint,
   ClearUnorderedAccessViewFloat,
   ClearView,
   Draw,
   DrawInstanced,
   DrawIndexed,
   DrawIndexedInstanced,
   DrawAuto,
   DrawInstancedIndirect,
   DrawIndexedInstancedIndirect,
   Dispatch,
   DispatchIndirect,
   FinishCommandList,
   ExecuteCommandList,
   Flush,

   Count,
};

using Functions = std::bitset<size_t(Function::Count)>;
} // namespace D3D11
} // namespace Hooks
} // namespace Luma
