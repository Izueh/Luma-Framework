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
   OMSetBlendState,
   OMSetDepthStencilState,
   IASetVertexBuffers,
   IASetIndexBuffer,
   ClearState,
   Map,
   Unmap,
   UpdateSubresource,
   CopyResource,
   CopySubresourceRegion,
   ResolveSubresource,
   ClearRenderTargetView,
   CopyStructureCount,
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

namespace D3D12 {
// The D3D12 functions hooked on request. Device creation, children's GetDevice and ExecuteCommandLists are always hooked. A
// graphics and compute pair (Set*RootSignature) is one function.
enum class Function : uint32_t
{
   // ID3D12Device
   CreatePipelineState, // Graphics, compute, stream, and those a game's pipeline library loads
   CreatePipelineLibrary,
   CreateRootSignature, // And CreateRootSignatureFromSubobjectInLibrary
   CreateStateObject, // And AddToStateObject
   CreateDescriptorHeap,
   CreateView, // Every kind of view and samplers
   CopyDescriptors, // CopyDescriptors and CopyDescriptorsSimple
   CreateResource, // Committed, placed and reserved resources, every version

   // ID3D12GraphicsCommandList
   Reset,
   ClearState,
   SetPipelineState, // And SetPipelineState1
   SetRootSignature,
   OMSetRenderTargets,
   SetDescriptorHeaps,
   SetRootArguments, // Tables, root descriptors and constants, graphics and compute
   SetFixedState, // RSSetViewports, RSSetScissorRects, IASetPrimitiveTopology, OMSetBlendFactor, OMSetStencilRef
   ResourceBarrier,
   Draw, // DrawInstanced, DrawIndexedInstanced, DispatchMesh
   Dispatch,
   ExecuteIndirect,
   ExecuteBundle,

   Count,
};

using Functions = std::bitset<size_t(Function::Count)>;
} // namespace D3D12
} // namespace Hooks
} // namespace Luma
