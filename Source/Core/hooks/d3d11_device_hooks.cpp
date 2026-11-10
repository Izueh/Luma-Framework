#include "hooks/hooks.hpp"

#include <initializer_list>
#include <optional>
#include <vector>

#include <d3d11_4.h>
#include <wrl/client.h>

#include "hooks/callbacks.hpp"
#include "hooks/detours.hpp"
#include "hooks/lifetime.hpp"
#include "hooks/slots.h"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
using Detours::original;

thread_local uint32_t device_depth = 0;

template <void (*Callback)(const void*)>
void Track(IUnknown* object)
{
   Lifetime::Track(object, &Detours::OnDestroyed<Callback>);
}

// Buffers and textures, reported with their versioned desc
template <auto Detour, auto Pre, auto Post, typename Desc1, typename Device, typename Desc, typename Resource>
HRESULT CreateResource(Device* device, const Desc* desc, const D3D11_SUBRESOURCE_DATA* initial_data, Resource** resource)
{
   Utils::DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !desc)
      return original<Detour>(device, desc, initial_data, resource);
   Desc1 game_desc = Detours::ToDesc1<Desc1>(*desc);
   Detours::Report(Pre, device, game_desc, initial_data);
   const Desc final_desc = Detours::ToDesc<Desc>(game_desc);
   const HRESULT result = original<Detour>(device, &final_desc, initial_data, resource);
   if (SUCCEEDED(result) && resource && *resource)
   {
      Scope::Guard scope;
      Track<Callbacks::OnResourceDestroyed>(*resource);
      Post(device, game_desc, initial_data, *resource);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer** buffer)
{
   return CreateResource<&CreateBuffer, &Callbacks::PreCreateBuffer, &Callbacks::PostCreateBuffer, D3D11_BUFFER_DESC>(device, desc, initial_data, buffer);
}

HRESULT STDMETHODCALLTYPE CreateTexture1D(ID3D11Device* device, const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D** texture)
{
   return CreateResource<&CreateTexture1D, &Callbacks::PreCreateTexture1D, &Callbacks::PostCreateTexture1D, D3D11_TEXTURE1D_DESC>(device, desc, initial_data, texture);
}

HRESULT STDMETHODCALLTYPE CreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D** texture)
{
   return CreateResource<&CreateTexture2D, &Callbacks::PreCreateTexture2D, &Callbacks::PostCreateTexture2D, D3D11_TEXTURE2D_DESC1>(device, desc, initial_data, texture);
}

HRESULT STDMETHODCALLTYPE CreateTexture2D1(ID3D11Device3* device, const D3D11_TEXTURE2D_DESC1* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D1** texture)
{
   return CreateResource<&CreateTexture2D1, &Callbacks::PreCreateTexture2D, &Callbacks::PostCreateTexture2D, D3D11_TEXTURE2D_DESC1>(device, desc, initial_data, texture);
}

HRESULT STDMETHODCALLTYPE CreateTexture3D(ID3D11Device* device, const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D** texture)
{
   return CreateResource<&CreateTexture3D, &Callbacks::PreCreateTexture3D, &Callbacks::PostCreateTexture3D, D3D11_TEXTURE3D_DESC1>(device, desc, initial_data, texture);
}

HRESULT STDMETHODCALLTYPE CreateTexture3D1(ID3D11Device3* device, const D3D11_TEXTURE3D_DESC1* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D1** texture)
{
   return CreateResource<&CreateTexture3D1, &Callbacks::PreCreateTexture3D, &Callbacks::PostCreateTexture3D, D3D11_TEXTURE3D_DESC1>(device, desc, initial_data, texture);
}

// Views, reported with their versioned desc
template <auto Detour, auto Pre, auto Post, typename Desc1, typename Device, typename Desc, typename View>
HRESULT CreateView(Device* device, ID3D11Resource* resource, const Desc* desc, View** view)
{
   Utils::DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard))
      return original<Detour>(device, resource, desc, view);
   std::optional<Desc1> game_desc;
   if (desc)
      game_desc = Detours::ToDesc1<Desc1>(*desc);
   Detours::Report(Pre, device, resource, game_desc);
   Desc final_desc = {};
   if (game_desc)
      final_desc = Detours::ToDesc<Desc>(*game_desc);
   const HRESULT result = original<Detour>(device, resource, game_desc ? &final_desc : nullptr, view);
   if (SUCCEEDED(result) && view && *view)
   {
      Scope::Guard scope;
      Track<Callbacks::OnViewDestroyed>(*view);
      Post(device, resource, game_desc ? &*game_desc : nullptr, *view);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,
   ID3D11ShaderResourceView** view)
{
   return CreateView<&CreateShaderResourceView, &Callbacks::PreCreateShaderResourceView, &Callbacks::PostCreateShaderResourceView,
      D3D11_SHADER_RESOURCE_VIEW_DESC1>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(ID3D11Device3* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc,
   ID3D11ShaderResourceView1** view)
{
   return CreateView<&CreateShaderResourceView1, &Callbacks::PreCreateShaderResourceView, &Callbacks::PostCreateShaderResourceView,
      D3D11_SHADER_RESOURCE_VIEW_DESC1>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC* desc,
   ID3D11RenderTargetView** view)
{
   return CreateView<&CreateRenderTargetView, &Callbacks::PreCreateRenderTargetView, &Callbacks::PostCreateRenderTargetView,
      D3D11_RENDER_TARGET_VIEW_DESC1>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateRenderTargetView1(ID3D11Device3* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc,
   ID3D11RenderTargetView1** view)
{
   return CreateView<&CreateRenderTargetView1, &Callbacks::PreCreateRenderTargetView, &Callbacks::PostCreateRenderTargetView,
      D3D11_RENDER_TARGET_VIEW_DESC1>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc,
   ID3D11UnorderedAccessView** view)
{
   return CreateView<&CreateUnorderedAccessView, &Callbacks::PreCreateUnorderedAccessView, &Callbacks::PostCreateUnorderedAccessView,
      D3D11_UNORDERED_ACCESS_VIEW_DESC1>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView1(ID3D11Device3* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc,
   ID3D11UnorderedAccessView1** view)
{
   return CreateView<&CreateUnorderedAccessView1, &Callbacks::PreCreateUnorderedAccessView, &Callbacks::PostCreateUnorderedAccessView,
      D3D11_UNORDERED_ACCESS_VIEW_DESC1>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc,
   ID3D11DepthStencilView** view)
{
   return CreateView<&CreateDepthStencilView, &Callbacks::PreCreateDepthStencilView, &Callbacks::PostCreateDepthStencilView,
      D3D11_DEPTH_STENCIL_VIEW_DESC>(device, resource, desc, view);
}

HRESULT STDMETHODCALLTYPE CreateSamplerState(ID3D11Device* device, const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** sampler)
{
   Utils::DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !desc)
      return original<&CreateSamplerState>(device, desc, sampler);
   D3D11_SAMPLER_DESC game_desc = *desc;
   Detours::Report(&Callbacks::PreCreateSamplerState, device, game_desc);
   const HRESULT result = original<&CreateSamplerState>(device, &game_desc, sampler);
   if (SUCCEEDED(result) && sampler && *sampler)
   {
      Scope::Guard scope;
      Track<Callbacks::OnSamplerDestroyed>(*sampler);
      Callbacks::PostCreateSamplerState(device, game_desc, *sampler);
   }
   return result;
}

D3D11_BLEND_DESC1 ToBlendDesc1(const D3D11_BLEND_DESC& desc)
{
   D3D11_BLEND_DESC1 desc1 = {desc.AlphaToCoverageEnable, desc.IndependentBlendEnable};
   for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
   {
      const D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[i];
      desc1.RenderTarget[i] = {rt.BlendEnable, FALSE, rt.SrcBlend, rt.DestBlend, rt.BlendOp, rt.SrcBlendAlpha, rt.DestBlendAlpha, rt.BlendOpAlpha,
         D3D11_LOGIC_OP_NOOP, rt.RenderTargetWriteMask};
   }
   return desc1;
}

D3D11_BLEND_DESC ToBlendDesc(const D3D11_BLEND_DESC1& desc1)
{
   D3D11_BLEND_DESC desc = {desc1.AlphaToCoverageEnable, desc1.IndependentBlendEnable};
   for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
   {
      const D3D11_RENDER_TARGET_BLEND_DESC1& rt = desc1.RenderTarget[i];
      desc.RenderTarget[i] = {rt.BlendEnable, rt.SrcBlend, rt.DestBlend, rt.BlendOp, rt.SrcBlendAlpha, rt.DestBlendAlpha, rt.BlendOpAlpha, rt.RenderTargetWriteMask};
   }
   return desc;
}

HRESULT STDMETHODCALLTYPE CreateBlendState(ID3D11Device* device, const D3D11_BLEND_DESC* desc, ID3D11BlendState** blend_state)
{
   Utils::DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !desc)
      return original<&CreateBlendState>(device, desc, blend_state);
   D3D11_BLEND_DESC1 game_desc = ToBlendDesc1(*desc);
   Detours::Report(&Callbacks::PreCreateBlendState, device, game_desc);
   const D3D11_BLEND_DESC final_desc = ToBlendDesc(game_desc);
   const HRESULT result = original<&CreateBlendState>(device, &final_desc, blend_state);
   if (SUCCEEDED(result) && blend_state && *blend_state)
   {
      Scope::Guard scope;
      Callbacks::PostCreateBlendState(device, game_desc, *blend_state);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateBlendState1(ID3D11Device1* device, const D3D11_BLEND_DESC1* desc, ID3D11BlendState1** blend_state)
{
   Utils::DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !desc)
      return original<&CreateBlendState1>(device, desc, blend_state);
   D3D11_BLEND_DESC1 game_desc = *desc;
   Detours::Report(&Callbacks::PreCreateBlendState, static_cast<ID3D11Device*>(device), game_desc);
   const HRESULT result = original<&CreateBlendState1>(device, &game_desc, blend_state);
   if (SUCCEEDED(result) && blend_state && *blend_state)
   {
      Scope::Guard scope;
      Callbacks::PostCreateBlendState(device, game_desc, *blend_state);
   }
   return result;
}

template <auto Detour, auto Pre, auto Post, typename Shader>
HRESULT CreateShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, Shader** shader)
{
   Utils::DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard))
      return original<Detour>(device, bytecode, length, linkage, shader);
   Detours::Report(Pre, device, bytecode, length);
   const HRESULT result = original<Detour>(device, bytecode, length, linkage, shader);
   if (SUCCEEDED(result) && shader && *shader)
   {
      Scope::Guard scope;
      Track<Callbacks::OnShaderDestroyed>(*shader);
      Post(device, bytecode, length, *shader);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateVertexShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader)
{
   return CreateShader<&CreateVertexShader, &Callbacks::PreCreateVertexShader, &Callbacks::PostCreateVertexShader>(device, bytecode, length, linkage, shader);
}

HRESULT STDMETHODCALLTYPE CreateHullShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11HullShader** shader)
{
   return CreateShader<&CreateHullShader, &Callbacks::PreCreateHullShader, &Callbacks::PostCreateHullShader>(device, bytecode, length, linkage, shader);
}

HRESULT STDMETHODCALLTYPE CreateDomainShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11DomainShader** shader)
{
   return CreateShader<&CreateDomainShader, &Callbacks::PreCreateDomainShader, &Callbacks::PostCreateDomainShader>(device, bytecode, length, linkage, shader);
}

HRESULT STDMETHODCALLTYPE CreateGeometryShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11GeometryShader** shader)
{
   return CreateShader<&CreateGeometryShader, &Callbacks::PreCreateGeometryShader, &Callbacks::PostCreateGeometryShader>(device, bytecode, length, linkage, shader);
}

HRESULT STDMETHODCALLTYPE CreatePixelShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11PixelShader** shader)
{
   return CreateShader<&CreatePixelShader, &Callbacks::PreCreatePixelShader, &Callbacks::PostCreatePixelShader>(device, bytecode, length, linkage, shader);
}

HRESULT STDMETHODCALLTYPE CreateComputeShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11ComputeShader** shader)
{
   return CreateShader<&CreateComputeShader, &Callbacks::PreCreateComputeShader, &Callbacks::PostCreateComputeShader>(device, bytecode, length, linkage, shader);
}

HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(ID3D11Device* device, const void* bytecode, SIZE_T length,
   const D3D11_SO_DECLARATION_ENTRY* entries, UINT entry_count, const UINT* strides, UINT stride_count, UINT rasterized_stream,
   ID3D11ClassLinkage* linkage, ID3D11GeometryShader** shader)
{
   Utils::DepthGuard guard(device_depth);
   const bool is_game = Detours::IsGameCall(guard);
   if (is_game)
      Detours::Report(&Callbacks::PreCreateGeometryShaderWithStreamOutput, device, bytecode, length);
   const HRESULT result = original<&CreateGeometryShaderWithStreamOutput>(device, bytecode, length, entries, entry_count, strides, stride_count,
      rasterized_stream, linkage, shader);
   if (is_game && SUCCEEDED(result) && shader && *shader)
   {
      Scope::Guard scope;
      Track<Callbacks::OnShaderDestroyed>(*shader);
      Callbacks::PostCreateGeometryShaderWithStreamOutput(device, bytecode, length, *shader);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* bytecode,
   SIZE_T length, ID3D11InputLayout** layout)
{
   Utils::DepthGuard guard(device_depth);
   const HRESULT result = original<&CreateInputLayout>(device, elements, count, bytecode, length, layout);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result) && layout && *layout)
   {
      Scope::Guard scope;
      Track<Callbacks::OnInputLayoutDestroyed>(*layout);
      Callbacks::PostCreateInputLayout(device, elements, count, bytecode, length, *layout);
   }
   return result;
}

void ReportSharedResource(ID3D11Device* device, HRESULT result, void** object)
{
   if (FAILED(result) || !object || !*object)
      return;
   Scope::Guard scope;
   ComPtr<ID3D11Resource> resource;
   if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&resource))))
      return;
   Track<Callbacks::OnResourceDestroyed>(resource.Get());
   Callbacks::PostOpenSharedResource(device, resource.Get());
}

HRESULT STDMETHODCALLTYPE OpenSharedResource(ID3D11Device* device, HANDLE handle, REFIID riid, void** object)
{
   Utils::DepthGuard guard(device_depth);
   const HRESULT result = original<&OpenSharedResource>(device, handle, riid, object);
   if (Detours::IsGameCall(guard))
      ReportSharedResource(device, result, object);
   return result;
}

HRESULT STDMETHODCALLTYPE OpenSharedResource1(ID3D11Device1* device, HANDLE handle, REFIID riid, void** object)
{
   Utils::DepthGuard guard(device_depth);
   const HRESULT result = original<&OpenSharedResource1>(device, handle, riid, object);
   if (Detours::IsGameCall(guard))
      ReportSharedResource(device, result, object);
   return result;
}

HRESULT STDMETHODCALLTYPE OpenSharedResourceByName(ID3D11Device1* device, LPCWSTR name, DWORD access, REFIID riid, void** object)
{
   Utils::DepthGuard guard(device_depth);
   const HRESULT result = original<&OpenSharedResourceByName>(device, name, access, riid, object);
   if (Detours::IsGameCall(guard))
      ReportSharedResource(device, result, object);
   return result;
}

template <auto Detour, typename Device, typename Context>
HRESULT CreateDeferredContextDetour(Device* device, UINT flags, Context** context)
{
   Utils::DepthGuard guard(device_depth);
   const HRESULT result = original<Detour>(device, flags, context);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result) && context && *context)
   {
      Scope::Guard scope;
      Track<Callbacks::OnDeferredContextDestroyed>(*context);
      Callbacks::PostCreateDeferredContext(device, *context);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext(ID3D11Device* device, UINT flags, ID3D11DeviceContext** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext>(device, flags, context);
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext1(ID3D11Device1* device, UINT flags, ID3D11DeviceContext1** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext1>(device, flags, context);
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext2(ID3D11Device2* device, UINT flags, ID3D11DeviceContext2** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext2>(device, flags, context);
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext3(ID3D11Device3* device, UINT flags, ID3D11DeviceContext3** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext3>(device, flags, context);
}

#define LUMA_DEVICE_HOOK(Type, Method, Version, Interchangeable)                                                         \
   Hook                                                                                                                  \
   {                                                                                                                     \
      VtableTarget{Vtables::Interface::D3D11Device, LumaSlot_##Type##_##Method, #Type "::" #Method, Version},          \
         reinterpret_cast<void*>(&Method), reinterpret_cast<void**>(&original<&Method>), Interchangeable                 \
   }
} // namespace

void InstallD3D11Device(const D3D11::Functions& functions)
{
   using D3D11::Function;
   std::vector<Hook> hooks;
   const auto add = [&](Function function, std::initializer_list<Hook> function_hooks) {
      if (functions.test(size_t(function)))
         hooks.insert(hooks.end(), function_hooks);
   };
   add(Function::CreateBuffer, {LUMA_DEVICE_HOOK(ID3D11Device, CreateBuffer, GUID_NULL, false)});
   add(Function::CreateTexture1D, {LUMA_DEVICE_HOOK(ID3D11Device, CreateTexture1D, GUID_NULL, false)});
   add(Function::CreateTexture2D, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateTexture2D, GUID_NULL, false),
      LUMA_DEVICE_HOOK(ID3D11Device3, CreateTexture2D1, __uuidof(ID3D11Device3), false),
   });
   add(Function::CreateTexture3D, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateTexture3D, GUID_NULL, false),
      LUMA_DEVICE_HOOK(ID3D11Device3, CreateTexture3D1, __uuidof(ID3D11Device3), false),
   });
   add(Function::OpenSharedResource, {
      LUMA_DEVICE_HOOK(ID3D11Device, OpenSharedResource, GUID_NULL, true),
      LUMA_DEVICE_HOOK(ID3D11Device1, OpenSharedResource1, __uuidof(ID3D11Device1), true),
      LUMA_DEVICE_HOOK(ID3D11Device1, OpenSharedResourceByName, __uuidof(ID3D11Device1), false),
   });
   add(Function::CreateShaderResourceView, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateShaderResourceView, GUID_NULL, false),
      LUMA_DEVICE_HOOK(ID3D11Device3, CreateShaderResourceView1, __uuidof(ID3D11Device3), false),
   });
   add(Function::CreateRenderTargetView, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateRenderTargetView, GUID_NULL, false),
      LUMA_DEVICE_HOOK(ID3D11Device3, CreateRenderTargetView1, __uuidof(ID3D11Device3), false),
   });
   add(Function::CreateUnorderedAccessView, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateUnorderedAccessView, GUID_NULL, false),
      LUMA_DEVICE_HOOK(ID3D11Device3, CreateUnorderedAccessView1, __uuidof(ID3D11Device3), false),
   });
   add(Function::CreateDepthStencilView, {LUMA_DEVICE_HOOK(ID3D11Device, CreateDepthStencilView, GUID_NULL, false)});
   add(Function::CreateSamplerState, {LUMA_DEVICE_HOOK(ID3D11Device, CreateSamplerState, GUID_NULL, false)});
   add(Function::CreateBlendState, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateBlendState, GUID_NULL, false),
      LUMA_DEVICE_HOOK(ID3D11Device1, CreateBlendState1, __uuidof(ID3D11Device1), false),
   });
   add(Function::CreateInputLayout, {LUMA_DEVICE_HOOK(ID3D11Device, CreateInputLayout, GUID_NULL, false)});
   add(Function::CreateVertexShader, {LUMA_DEVICE_HOOK(ID3D11Device, CreateVertexShader, GUID_NULL, false)});
   add(Function::CreateHullShader, {LUMA_DEVICE_HOOK(ID3D11Device, CreateHullShader, GUID_NULL, false)});
   add(Function::CreateDomainShader, {LUMA_DEVICE_HOOK(ID3D11Device, CreateDomainShader, GUID_NULL, false)});
   add(Function::CreateGeometryShader, {LUMA_DEVICE_HOOK(ID3D11Device, CreateGeometryShader, GUID_NULL, false)});
   add(Function::CreateGeometryShaderWithStreamOutput, {LUMA_DEVICE_HOOK(ID3D11Device, CreateGeometryShaderWithStreamOutput, GUID_NULL, false)});
   add(Function::CreatePixelShader, {LUMA_DEVICE_HOOK(ID3D11Device, CreatePixelShader, GUID_NULL, false)});
   add(Function::CreateComputeShader, {LUMA_DEVICE_HOOK(ID3D11Device, CreateComputeShader, GUID_NULL, false)});
   add(Function::CreateDeferredContext, {
      LUMA_DEVICE_HOOK(ID3D11Device, CreateDeferredContext, GUID_NULL, true),
      LUMA_DEVICE_HOOK(ID3D11Device1, CreateDeferredContext1, __uuidof(ID3D11Device1), true),
      LUMA_DEVICE_HOOK(ID3D11Device2, CreateDeferredContext2, __uuidof(ID3D11Device2), true),
      LUMA_DEVICE_HOOK(ID3D11Device3, CreateDeferredContext3, __uuidof(ID3D11Device3), true),
   });
   if (!hooks.empty())
      Declare(hooks);
}
} // namespace Hooks
} // namespace Luma
