#include "Hooks/Hooks.hpp"

#include <initializer_list>
#include <optional>
#include <vector>

#include <d3d11_4.h>
#include <intrin.h>
#include <wrl/client.h>

#include "Handlers/D3D11/D3D11Handlers.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/Lifetime.hpp"
#include "Hooks/Slots.h"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
using Detours::original;

thread_local uint32_t device_depth = 0;

template <auto Callback>
void Track(ID3D11Device* device, IUnknown* object)
{
   Lifetime::Track(object, device, &Detours::OnDestroyed<Callback>);
}

// Buffers and textures, reported with their versioned desc
template <auto Detour, auto Pre, auto Post, typename Desc1, typename Device, typename Desc, typename Resource>
HRESULT CreateResource(Device* device, const Desc* desc, const D3D11_SUBRESOURCE_DATA* initial_data, Resource** resource, const void* caller)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, caller) || !desc)
      return original<Detour>(device, desc, initial_data, resource);
   Desc1 game_desc = Detours::ToDesc1<Desc1>(*desc);
   Detours::Report(Pre, device, game_desc, initial_data);
   const Desc final_desc = Detours::ToDesc<Desc>(game_desc);
   const HRESULT result = original<Detour>(device, &final_desc, initial_data, resource);
   if (SUCCEEDED(result) && resource && *resource)
   {
      Scope::Guard scope;
      Track<Handlers::OnResourceDestroyed>(device, *resource);
      Post(device, game_desc, initial_data, *resource);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer** buffer)
{
   return CreateResource<&CreateBuffer, &Handlers::PreCreateBuffer, &Handlers::PostCreateBuffer, D3D11_BUFFER_DESC>(device, desc, initial_data, buffer, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateTexture1D(ID3D11Device* device, const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D** texture)
{
   return CreateResource<&CreateTexture1D, &Handlers::PreCreateTexture1D, &Handlers::PostCreateTexture1D, D3D11_TEXTURE1D_DESC>(device, desc, initial_data, texture, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D** texture)
{
   return CreateResource<&CreateTexture2D, &Handlers::PreCreateTexture2D, &Handlers::PostCreateTexture2D, D3D11_TEXTURE2D_DESC1>(device, desc, initial_data, texture, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateTexture2D1(ID3D11Device3* device, const D3D11_TEXTURE2D_DESC1* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D1** texture)
{
   return CreateResource<&CreateTexture2D1, &Handlers::PreCreateTexture2D, &Handlers::PostCreateTexture2D, D3D11_TEXTURE2D_DESC1>(device, desc, initial_data, texture, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateTexture3D(ID3D11Device* device, const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D** texture)
{
   return CreateResource<&CreateTexture3D, &Handlers::PreCreateTexture3D, &Handlers::PostCreateTexture3D, D3D11_TEXTURE3D_DESC1>(device, desc, initial_data, texture, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateTexture3D1(ID3D11Device3* device, const D3D11_TEXTURE3D_DESC1* desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D1** texture)
{
   return CreateResource<&CreateTexture3D1, &Handlers::PreCreateTexture3D, &Handlers::PostCreateTexture3D, D3D11_TEXTURE3D_DESC1>(device, desc, initial_data, texture, _ReturnAddress());
}

// Views, reported with their versioned desc
template <auto Detour, auto Pre, auto Post, typename Desc1, typename Device, typename Desc, typename View>
HRESULT CreateView(Device* device, ID3D11Resource* resource, const Desc* desc, View** view, const void* caller)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, caller))
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
      Track<Handlers::OnViewDestroyed>(device, *view);
      Post(device, resource, game_desc ? &*game_desc : nullptr, *view);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,
   ID3D11ShaderResourceView** view)
{
   return CreateView<&CreateShaderResourceView, &Handlers::PreCreateShaderResourceView, &Handlers::PostCreateShaderResourceView,
      D3D11_SHADER_RESOURCE_VIEW_DESC1>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(ID3D11Device3* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc,
   ID3D11ShaderResourceView1** view)
{
   return CreateView<&CreateShaderResourceView1, &Handlers::PreCreateShaderResourceView, &Handlers::PostCreateShaderResourceView,
      D3D11_SHADER_RESOURCE_VIEW_DESC1>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC* desc,
   ID3D11RenderTargetView** view)
{
   return CreateView<&CreateRenderTargetView, &Handlers::PreCreateRenderTargetView, &Handlers::PostCreateRenderTargetView,
      D3D11_RENDER_TARGET_VIEW_DESC1>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateRenderTargetView1(ID3D11Device3* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc,
   ID3D11RenderTargetView1** view)
{
   return CreateView<&CreateRenderTargetView1, &Handlers::PreCreateRenderTargetView, &Handlers::PostCreateRenderTargetView,
      D3D11_RENDER_TARGET_VIEW_DESC1>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc,
   ID3D11UnorderedAccessView** view)
{
   return CreateView<&CreateUnorderedAccessView, &Handlers::PreCreateUnorderedAccessView, &Handlers::PostCreateUnorderedAccessView,
      D3D11_UNORDERED_ACCESS_VIEW_DESC1>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView1(ID3D11Device3* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc,
   ID3D11UnorderedAccessView1** view)
{
   return CreateView<&CreateUnorderedAccessView1, &Handlers::PreCreateUnorderedAccessView, &Handlers::PostCreateUnorderedAccessView,
      D3D11_UNORDERED_ACCESS_VIEW_DESC1>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc,
   ID3D11DepthStencilView** view)
{
   return CreateView<&CreateDepthStencilView, &Handlers::PreCreateDepthStencilView, &Handlers::PostCreateDepthStencilView,
      D3D11_DEPTH_STENCIL_VIEW_DESC>(device, resource, desc, view, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateSamplerState(ID3D11Device* device, const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** sampler)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()) || !desc)
      return original<&CreateSamplerState>(device, desc, sampler);
   D3D11_SAMPLER_DESC game_desc = *desc;
   Detours::Report(&Handlers::PreCreateSamplerState, device, game_desc);
   const HRESULT result = original<&CreateSamplerState>(device, &game_desc, sampler);
   if (SUCCEEDED(result) && sampler && *sampler)
   {
      Scope::Guard scope;
      Track<Handlers::OnSamplerDestroyed>(device, *sampler);
      Handlers::PostCreateSamplerState(device, game_desc, *sampler);
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
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()) || !desc)
      return original<&CreateBlendState>(device, desc, blend_state);
   D3D11_BLEND_DESC1 game_desc = ToBlendDesc1(*desc);
   Detours::Report(&Handlers::PreCreateBlendState, device, game_desc);
   const D3D11_BLEND_DESC final_desc = ToBlendDesc(game_desc);
   const HRESULT result = original<&CreateBlendState>(device, &final_desc, blend_state);
   if (SUCCEEDED(result) && blend_state && *blend_state)
   {
      Scope::Guard scope;
      Handlers::PostCreateBlendState(device, game_desc, *blend_state);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateBlendState1(ID3D11Device1* device, const D3D11_BLEND_DESC1* desc, ID3D11BlendState1** blend_state)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()) || !desc)
      return original<&CreateBlendState1>(device, desc, blend_state);
   D3D11_BLEND_DESC1 game_desc = *desc;
   Detours::Report(&Handlers::PreCreateBlendState, static_cast<ID3D11Device*>(device), game_desc);
   const HRESULT result = original<&CreateBlendState1>(device, &game_desc, blend_state);
   if (SUCCEEDED(result) && blend_state && *blend_state)
   {
      Scope::Guard scope;
      Handlers::PostCreateBlendState(device, game_desc, *blend_state);
   }
   return result;
}

template <auto Detour, ShaderStage Stage, typename Shader>
HRESULT CreateShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, Shader** shader, const void* caller)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, caller))
      return original<Detour>(device, bytecode, length, linkage, shader);
   Detours::Report(&Handlers::PreCreateShader, device, Stage, bytecode, length);
   const HRESULT result = original<Detour>(device, bytecode, length, linkage, shader);
   if (SUCCEEDED(result) && shader && *shader)
   {
      Scope::Guard scope;
      Track<Handlers::OnShaderDestroyed>(device, *shader);
      Handlers::PostCreateShader(device, Stage, bytecode, length, false, *shader);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateVertexShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader)
{
   return CreateShader<&CreateVertexShader, ShaderStage::Vertex>(device, bytecode, length, linkage, shader, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateHullShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11HullShader** shader)
{
   return CreateShader<&CreateHullShader, ShaderStage::Hull>(device, bytecode, length, linkage, shader, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateDomainShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11DomainShader** shader)
{
   return CreateShader<&CreateDomainShader, ShaderStage::Domain>(device, bytecode, length, linkage, shader, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateGeometryShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11GeometryShader** shader)
{
   return CreateShader<&CreateGeometryShader, ShaderStage::Geometry>(device, bytecode, length, linkage, shader, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreatePixelShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11PixelShader** shader)
{
   return CreateShader<&CreatePixelShader, ShaderStage::Pixel>(device, bytecode, length, linkage, shader, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateComputeShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11ComputeShader** shader)
{
   return CreateShader<&CreateComputeShader, ShaderStage::Compute>(device, bytecode, length, linkage, shader, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(ID3D11Device* device, const void* bytecode, SIZE_T length,
   const D3D11_SO_DECLARATION_ENTRY* entries, UINT entry_count, const UINT* strides, UINT stride_count, UINT rasterized_stream,
   ID3D11ClassLinkage* linkage, ID3D11GeometryShader** shader)
{
   DepthGuard guard(device_depth);
   const bool is_game = Detours::IsGameCall(guard, _ReturnAddress());
   if (is_game)
      Detours::Report(&Handlers::PreCreateShader, device, ShaderStage::Geometry, bytecode, length);
   const HRESULT result = original<&CreateGeometryShaderWithStreamOutput>(device, bytecode, length, entries, entry_count, strides, stride_count,
      rasterized_stream, linkage, shader);
   if (is_game && SUCCEEDED(result) && shader && *shader)
   {
      Scope::Guard scope;
      Track<Handlers::OnShaderDestroyed>(device, *shader);
      Handlers::PostCreateShader(device, ShaderStage::Geometry, bytecode, length, true, *shader);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* bytecode,
   SIZE_T length, ID3D11InputLayout** layout)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreateInputLayout>(device, elements, count, bytecode, length, layout);
   if (Detours::IsGameCall(guard, _ReturnAddress()) && SUCCEEDED(result) && layout && *layout)
   {
      Scope::Guard scope;
      Track<Handlers::OnInputLayoutDestroyed>(device, *layout);
      Handlers::PostCreateInputLayout(device, elements, count, bytecode, length, *layout);
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
   Track<Handlers::OnResourceDestroyed>(device, resource.Get());
   Handlers::PostOpenSharedResource(device, resource.Get());
}

HRESULT STDMETHODCALLTYPE OpenSharedResource(ID3D11Device* device, HANDLE handle, REFIID riid, void** object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&OpenSharedResource>(device, handle, riid, object);
   if (Detours::IsGameCall(guard, _ReturnAddress()))
      ReportSharedResource(device, result, object);
   return result;
}

HRESULT STDMETHODCALLTYPE OpenSharedResource1(ID3D11Device1* device, HANDLE handle, REFIID riid, void** object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&OpenSharedResource1>(device, handle, riid, object);
   if (Detours::IsGameCall(guard, _ReturnAddress()))
      ReportSharedResource(device, result, object);
   return result;
}

HRESULT STDMETHODCALLTYPE OpenSharedResourceByName(ID3D11Device1* device, LPCWSTR name, DWORD access, REFIID riid, void** object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&OpenSharedResourceByName>(device, name, access, riid, object);
   if (Detours::IsGameCall(guard, _ReturnAddress()))
      ReportSharedResource(device, result, object);
   return result;
}

template <auto Detour, typename Device, typename Context>
HRESULT CreateDeferredContextDetour(Device* device, UINT flags, Context** context, const void* caller)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<Detour>(device, flags, context);
   if (Detours::IsGameCall(guard, caller) && SUCCEEDED(result) && context && *context)
   {
      Scope::Guard scope;
      Track<Handlers::OnDeferredContextDestroyed>(device, *context);
      ObserveContext(static_cast<ID3D11DeviceContext*>(*context));
      Handlers::PostCreateDeferredContext(device, *context);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext(ID3D11Device* device, UINT flags, ID3D11DeviceContext** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext>(device, flags, context, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext1(ID3D11Device1* device, UINT flags, ID3D11DeviceContext1** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext1>(device, flags, context, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext2(ID3D11Device2* device, UINT flags, ID3D11DeviceContext2** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext2>(device, flags, context, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateDeferredContext3(ID3D11Device3* device, UINT flags, ID3D11DeviceContext3** context)
{
   return CreateDeferredContextDetour<&CreateDeferredContext3>(device, flags, context, _ReturnAddress());
}

// A device method's hook, Detour being its detour
template <auto Detour>
Hook DeviceHook(uint32_t slot, const char* name, const IID& version, bool interchangeable)
{
   return {VtableTarget{Vtables::Interface::D3D11Device, slot, name, version}, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original<Detour>), interchangeable};
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
   add(Function::CreateBuffer, {DeviceHook<&CreateBuffer>(LumaSlot_ID3D11Device_CreateBuffer, "ID3D11Device::CreateBuffer", GUID_NULL, false)});
   add(Function::CreateTexture1D, {DeviceHook<&CreateTexture1D>(LumaSlot_ID3D11Device_CreateTexture1D, "ID3D11Device::CreateTexture1D", GUID_NULL, false)});
   add(Function::CreateTexture2D, {
                                     DeviceHook<&CreateTexture2D>(LumaSlot_ID3D11Device_CreateTexture2D, "ID3D11Device::CreateTexture2D", GUID_NULL, false),
                                     DeviceHook<&CreateTexture2D1>(LumaSlot_ID3D11Device3_CreateTexture2D1, "ID3D11Device3::CreateTexture2D1", __uuidof(ID3D11Device3), false),
                                  });
   add(Function::CreateTexture3D, {
                                     DeviceHook<&CreateTexture3D>(LumaSlot_ID3D11Device_CreateTexture3D, "ID3D11Device::CreateTexture3D", GUID_NULL, false),
                                     DeviceHook<&CreateTexture3D1>(LumaSlot_ID3D11Device3_CreateTexture3D1, "ID3D11Device3::CreateTexture3D1", __uuidof(ID3D11Device3), false),
                                  });
   add(Function::OpenSharedResource, {
                                        DeviceHook<&OpenSharedResource>(LumaSlot_ID3D11Device_OpenSharedResource, "ID3D11Device::OpenSharedResource", GUID_NULL, true),
                                        DeviceHook<&OpenSharedResource1>(LumaSlot_ID3D11Device1_OpenSharedResource1, "ID3D11Device1::OpenSharedResource1", __uuidof(ID3D11Device1), true),
                                        DeviceHook<&OpenSharedResourceByName>(LumaSlot_ID3D11Device1_OpenSharedResourceByName, "ID3D11Device1::OpenSharedResourceByName", __uuidof(ID3D11Device1), false),
                                     });
   add(Function::CreateShaderResourceView, {
                                              DeviceHook<&CreateShaderResourceView>(LumaSlot_ID3D11Device_CreateShaderResourceView, "ID3D11Device::CreateShaderResourceView", GUID_NULL, false),
                                              DeviceHook<&CreateShaderResourceView1>(LumaSlot_ID3D11Device3_CreateShaderResourceView1, "ID3D11Device3::CreateShaderResourceView1", __uuidof(ID3D11Device3), false),
                                           });
   add(Function::CreateRenderTargetView, {
                                            DeviceHook<&CreateRenderTargetView>(LumaSlot_ID3D11Device_CreateRenderTargetView, "ID3D11Device::CreateRenderTargetView", GUID_NULL, false),
                                            DeviceHook<&CreateRenderTargetView1>(LumaSlot_ID3D11Device3_CreateRenderTargetView1, "ID3D11Device3::CreateRenderTargetView1", __uuidof(ID3D11Device3), false),
                                         });
   add(Function::CreateUnorderedAccessView, {
                                               DeviceHook<&CreateUnorderedAccessView>(LumaSlot_ID3D11Device_CreateUnorderedAccessView, "ID3D11Device::CreateUnorderedAccessView", GUID_NULL, false),
                                               DeviceHook<&CreateUnorderedAccessView1>(LumaSlot_ID3D11Device3_CreateUnorderedAccessView1, "ID3D11Device3::CreateUnorderedAccessView1", __uuidof(ID3D11Device3), false),
                                            });
   add(Function::CreateDepthStencilView, {DeviceHook<&CreateDepthStencilView>(LumaSlot_ID3D11Device_CreateDepthStencilView, "ID3D11Device::CreateDepthStencilView", GUID_NULL, false)});
   add(Function::CreateSamplerState, {DeviceHook<&CreateSamplerState>(LumaSlot_ID3D11Device_CreateSamplerState, "ID3D11Device::CreateSamplerState", GUID_NULL, false)});
   add(Function::CreateBlendState, {
                                      DeviceHook<&CreateBlendState>(LumaSlot_ID3D11Device_CreateBlendState, "ID3D11Device::CreateBlendState", GUID_NULL, false),
                                      DeviceHook<&CreateBlendState1>(LumaSlot_ID3D11Device1_CreateBlendState1, "ID3D11Device1::CreateBlendState1", __uuidof(ID3D11Device1), false),
                                   });
   add(Function::CreateInputLayout, {DeviceHook<&CreateInputLayout>(LumaSlot_ID3D11Device_CreateInputLayout, "ID3D11Device::CreateInputLayout", GUID_NULL, false)});
   add(Function::CreateVertexShader, {DeviceHook<&CreateVertexShader>(LumaSlot_ID3D11Device_CreateVertexShader, "ID3D11Device::CreateVertexShader", GUID_NULL, false)});
   add(Function::CreateHullShader, {DeviceHook<&CreateHullShader>(LumaSlot_ID3D11Device_CreateHullShader, "ID3D11Device::CreateHullShader", GUID_NULL, false)});
   add(Function::CreateDomainShader, {DeviceHook<&CreateDomainShader>(LumaSlot_ID3D11Device_CreateDomainShader, "ID3D11Device::CreateDomainShader", GUID_NULL, false)});
   add(Function::CreateGeometryShader, {DeviceHook<&CreateGeometryShader>(LumaSlot_ID3D11Device_CreateGeometryShader, "ID3D11Device::CreateGeometryShader", GUID_NULL, false)});
   add(Function::CreateGeometryShaderWithStreamOutput, {DeviceHook<&CreateGeometryShaderWithStreamOutput>(LumaSlot_ID3D11Device_CreateGeometryShaderWithStreamOutput, "ID3D11Device::CreateGeometryShaderWithStreamOutput", GUID_NULL, false)});
   add(Function::CreatePixelShader, {DeviceHook<&CreatePixelShader>(LumaSlot_ID3D11Device_CreatePixelShader, "ID3D11Device::CreatePixelShader", GUID_NULL, false)});
   add(Function::CreateComputeShader, {DeviceHook<&CreateComputeShader>(LumaSlot_ID3D11Device_CreateComputeShader, "ID3D11Device::CreateComputeShader", GUID_NULL, false)});
   add(Function::CreateDeferredContext, {
                                           DeviceHook<&CreateDeferredContext>(LumaSlot_ID3D11Device_CreateDeferredContext, "ID3D11Device::CreateDeferredContext", GUID_NULL, true),
                                           DeviceHook<&CreateDeferredContext1>(LumaSlot_ID3D11Device1_CreateDeferredContext1, "ID3D11Device1::CreateDeferredContext1", __uuidof(ID3D11Device1), true),
                                           DeviceHook<&CreateDeferredContext2>(LumaSlot_ID3D11Device2_CreateDeferredContext2, "ID3D11Device2::CreateDeferredContext2", __uuidof(ID3D11Device2), true),
                                           DeviceHook<&CreateDeferredContext3>(LumaSlot_ID3D11Device3_CreateDeferredContext3, "ID3D11Device3::CreateDeferredContext3", __uuidof(ID3D11Device3), true),
                                        });
   if (!hooks.empty())
      Declare(hooks);
}
} // namespace Hooks
} // namespace Luma
